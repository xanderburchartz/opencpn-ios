// Tests for sync.mjs against a local fake App Store Connect and a fake GitHub.
// No network, no real key: a throwaway EC key is made for each run.
//
// Usage: node scripts/testflight-feedback/test.mjs
//
// SPDX-License-Identifier: GPL-2.0-or-later

import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { run, summarizeCrash, LABEL } from "./sync.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const fixtures = path.join(here, "fixtures");
const json = (f) => JSON.parse(fs.readFileSync(path.join(fixtures, f), "utf8"));

const { privateKey, publicKey } = crypto.generateKeyPairSync("ec", { namedCurve: "prime256v1" });
const pem = privateKey.export({ type: "pkcs8", format: "pem" });

function verifyJwt(auth) {
  const m = /^Bearer (.+)\.(.+)\.(.+)$/.exec(auth || "");
  assert.ok(m, "Authorization header with a JWT");
  const head = JSON.parse(Buffer.from(m[1], "base64url"));
  const body = JSON.parse(Buffer.from(m[2], "base64url"));
  assert.deepEqual(head, { alg: "ES256", kid: "TESTKEY123", typ: "JWT" });
  assert.equal(body.iss, "issuer-uuid");
  assert.equal(body.aud, "appstoreconnect-v1");
  assert.ok(body.exp - body.iat <= 20 * 60, "token lives at most 20 minutes");
  const ok = crypto.verify("sha256", Buffer.from(`${m[1]}.${m[2]}`), { key: publicKey, dsaEncoding: "ieee-p1363" },
    Buffer.from(m[3], "base64url"));
  assert.ok(ok, "JWT signature verifies with the public key");
}

// Fake servers. Screenshot submissions come in two pages to test links.next.
const state = { issues: [], labels: [], ascRequests: [] };
let base = "";
const server = http.createServer((req, res) => {
  const url = new URL(req.url, "http://x");
  const send = (code, obj) => {
    res.writeHead(code, { "Content-Type": "application/json" });
    res.end(JSON.stringify(obj));
  };
  let body = "";
  req.on("data", (c) => (body += c));
  req.on("end", () => {
    if (url.pathname.startsWith("/asc/")) {
      verifyJwt(req.headers.authorization);
      state.ascRequests.push(url);
      const p = url.pathname.slice(4);
      if (p === "/v1/apps/6838640154/betaFeedbackScreenshotSubmissions") {
        const all = json("screenshot-submissions.json");
        if (url.searchParams.get("cursor") === "2") return send(200, { data: all.data.slice(1), included: all.included, links: {} });
        return send(200, { data: all.data.slice(0, 1), included: all.included,
          links: { next: `${base}/asc${p}?cursor=2` } });
      }
      if (p === "/v1/apps/6838640154/betaFeedbackCrashSubmissions") return send(200, json("crash-submissions.json"));
      const cl = /^\/v1\/betaFeedbackCrashSubmissions\/([^/]+)\/crashLog$/.exec(p);
      if (cl) {
        const f = path.join(fixtures, `crashlog-${cl[1]}.txt`);
        return send(200, { data: { type: "betaCrashLogs", id: "x", attributes: { logText: fs.readFileSync(f, "utf8") } } });
      }
      return send(404, { errors: [{ status: "404" }] });
    }
    if (url.pathname.startsWith("/gh/")) {
      assert.equal(req.headers.authorization, "Bearer gh-test-token");
      const p = url.pathname.slice(3);
      if (req.method === "GET" && p === "/repos/o/r/issues") {
        assert.equal(url.searchParams.get("labels"), LABEL);
        assert.equal(url.searchParams.get("state"), "all");
        const per = Number(url.searchParams.get("per_page"));
        const page = Number(url.searchParams.get("page"));
        return send(200, state.issues.slice((page - 1) * per, page * per));
      }
      if (req.method === "GET" && p === `/repos/o/r/labels/${LABEL}`) {
        return state.labels.includes(LABEL) ? send(200, { name: LABEL }) : send(404, {});
      }
      if (req.method === "POST" && p === "/repos/o/r/labels") {
        state.labels.push(JSON.parse(body).name);
        return send(201, {});
      }
      if (req.method === "POST" && p === "/repos/o/r/issues") {
        const issue = { ...JSON.parse(body), number: state.issues.length + 1 };
        state.issues.push(issue);
        return send(201, issue);
      }
    }
    send(404, {});
  });
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
base = `http://127.0.0.1:${server.address().port}`;

const env = {
  ASC_KEY_ID: "TESTKEY123",
  ASC_ISSUER_ID: "issuer-uuid",
  ASC_PRIVATE_KEY: pem.replace(/\n/g, "\\n"), // one-line form must work too
  ASC_APP_ID: "6838640154",
  ASC_API_BASE: `${base}/asc`,
  GITHUB_TOKEN: "gh-test-token",
  GITHUB_REPOSITORY: "o/r",
  GITHUB_API_URL: `${base}/gh`,
};
const quiet = () => {};
let failures = 0;
async function test(name, fn) {
  try {
    await fn();
    console.log(`ok   ${name}`);
  } catch (e) {
    failures++;
    console.log(`FAIL ${name}\n     ${e.stack.split("\n").slice(0, 3).join("\n     ")}`);
  }
}

await test("dry run posts nothing", async () => {
  const r = await run(["--dry-run"], env, quiet);
  assert.equal(r.created.length, 4);
  assert.equal(state.issues.length, 0);
  assert.equal(state.labels.length, 0);
});

await test("Apple is not asked for email or tester", () => {
  for (const u of state.ascRequests.filter((x) => x.pathname.includes("/apps/") && !x.searchParams.get("cursor"))) {
    const fields = [...u.searchParams].filter(([k]) => k.startsWith("fields[betaFeedback")).map(([, v]) => v).join(",");
    assert.ok(fields.includes("comment") && fields.includes("build"), fields);
    assert.ok(!/\bemail\b|\btester\b/.test(fields), fields);
    assert.equal(u.searchParams.get("include"), "build");
  }
});

await test("--max limits the number of issues", async () => {
  const r = await run(["--max", "1"], env, quiet);
  assert.equal(r.created.length, 1);
  assert.equal(state.issues.length, 1);
  assert.deepEqual(state.labels, [LABEL]);
  assert.match(state.issues[0].body, /tf-feedback-id: AAA111-screenshot-1 -->/, "oldest first");
});

await test("posts the rest, both pages, without duplicates", async () => {
  const r = await run([], env, quiet);
  assert.equal(r.created.length, 3);
  assert.equal(state.issues.length, 4);
  const ids = state.issues.map((i) => /tf-feedback-id: (\S+) -->/.exec(i.body)[1]).sort();
  assert.deepEqual(ids, ["AAA111-screenshot-1", "AAA111-screenshot-2", "BBB222-crash-1", "BBB222-crash-2"]);
  for (const i of state.issues) assert.deepEqual(i.labels, [LABEL]);
});

await test("second run creates nothing", async () => {
  const r = await run([], env, quiet);
  assert.equal(r.created.length, 0);
  assert.equal(state.issues.length, 4);
  assert.equal(state.labels.length, 1, "label created once");
});

await test("no personal data in issues", () => {
  const all = JSON.stringify(state.issues);
  for (const bad of ["tester@example.com", "tester-xyz", "Europe/London", "en-GB", "example.invalid",
    "0A1B2C3D-4E5F", "6F1D2C3B", "1A2B3C4D-5E6F", "abcdef0123456789", "/private/var"]) {
    assert.ok(!all.includes(bad), `found ${bad}`);
  }
});

const byId = (id) => state.issues.find((i) => i.body.includes(`<!-- tf-feedback-id: ${id} -->`));

await test("issue content", () => {
  const shot = byId("AAA111-screenshot-1");
  assert.match(shot.title, /^TestFlight feedback \(build 9\): Route manager/);
  assert.match(shot.body, /\| Build \| 9 \|/);
  assert.match(shot.body, /\| Device \| iPhone17,2 \|/);
  assert.match(shot.body, /\| OS \| iOS 26\.1 \|/);
  assert.match(shot.body, /\| Date \| 2026-11-27 09:04:11 UTC \|/);
  assert.match(shot.body, /Screenshot available to the maintainer\./);
  assert.ok(!shot.body.includes("@someone"), "mention defused");
  assert.equal((shot.body.match(/<!-- tf-feedback-id/g) || []).length, 1, "tester cannot add a marker");
  const two = byId("AAA111-screenshot-2");
  assert.match(two.body, /2 screenshots available to the maintainer\./);
  assert.match(two.body, /\| OS \| iPadOS 26\.5 \|/);
  const crash = byId("BBB222-crash-1");
  assert.match(crash.title, /^TestFlight crash \(build 9\): Crashed when I turned on the radar plugin/);
  assert.match(crash.body, /Exception: EXC_BAD_ACCESS \(SIGSEGV\)/);
  assert.match(crash.body, /RadarControl::Start\(\)/);
  const ips = byId("BBB222-crash-2");
  assert.match(ips.title, /^TestFlight crash \(build 9\): EXC_CRASH \(SIGABRT\)/);
  assert.match(ips.body, /Thread 2, top frames:\n0 libsystem_kernel\.dylib __pthread_kill \+ 8/);
});

await test("crash summary keeps at most 8 frames", () => {
  const s = summarizeCrash(fs.readFileSync(path.join(fixtures, "crashlog-BBB222-crash-1.txt"), "utf8"));
  assert.equal(s.frames.length, 8);
  assert.equal(summarizeCrash(""), null);
});

await test("--since skips older feedback", async () => {
  const saved = state.issues.splice(0);
  const r = await run(["--dry-run", "--since", "2026-11-28"], env, quiet);
  assert.equal(r.created.length, 2);
  state.issues.push(...saved);
});

await test("feedback from before START is never posted", async () => {
  assert.ok(!JSON.stringify(state.issues).includes("AAA111-screenshot-0"));
  const r = await run(["--dry-run", "--since", "2020-01-01"], { ...env, GITHUB_TOKEN: "" }, quiet);
  assert.equal(r.created.length, 4, "an earlier --since does not move the start back");
  assert.ok(!r.created.some((i) => i.body.includes("AAA111-screenshot-0")));
});

await test("missing key is a clear error", async () => {
  await assert.rejects(run([], { ...env, ASC_PRIVATE_KEY: "" }, quiet), /ASC_PRIVATE_KEY/);
});

server.close();
if (failures) {
  console.log(`${failures} failed`);
  process.exit(1);
}
console.log("all tests passed");

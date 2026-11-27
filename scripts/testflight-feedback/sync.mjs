#!/usr/bin/env node
// Copies new TestFlight feedback (screenshot feedback and crash feedback) of one app from the
// App Store Connect API to GitHub issues, one issue per submission.
//
// Privacy: the tester's name, email address and tester ID are never requested from Apple
// (sparse fieldsets below) and never written to an issue. Screenshots are not published; the
// issue only says that one is available to the maintainer. For a crash, only a short summary
// (exception, termination reason, top frames of the crashed thread) is posted.
//
// Duplicates: every issue carries a hidden marker <!-- tf-feedback-id: <id> -->. Before
// posting, all issues with the label are read and their markers collected.
//
// No dependencies: Node 20 or newer (fetch, crypto).
//
// Environment:
//   ASC_KEY_ID, ASC_ISSUER_ID, ASC_PRIVATE_KEY (PEM text of the .p8 key), ASC_APP_ID
//   GITHUB_TOKEN, GITHUB_REPOSITORY (owner/name)
//   ASC_API_BASE, GITHUB_API_URL (optional, for tests)
//
// Usage:
//   node sync.mjs [--dry-run] [--fixture DIR] [--max N] [--since YYYY-MM-DD]
//   --since can only move the start later than START.
//
// SPDX-License-Identifier: GPL-2.0-or-later

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const LABEL = "testflight-feedback";
// Testers were told that feedback becomes public from this moment on (What to Test of build 9,
// saved 2026-11-27 06:02:18 UTC). Older feedback is never posted, whatever the options.
export const START = "2026-11-27T06:02:18Z";
const MARKER_RE = /<!-- tf-feedback-id: ([A-Za-z0-9._-]+) -->/g;
const MAX_FRAMES = 8;

// Fields asked from Apple. email and tester are deliberately left out.
const SUBMISSION_FIELDS = [
  "createdDate", "comment", "deviceModel", "osVersion", "appPlatform",
  "devicePlatform", "deviceFamily", "build",
];

// ---------------------------------------------------------------- App Store Connect

export function makeJwt(keyId, issuerId, privateKeyPem, now = Math.floor(Date.now() / 1000)) {
  const b64 = (obj) => Buffer.from(JSON.stringify(obj)).toString("base64url");
  const head = b64({ alg: "ES256", kid: keyId, typ: "JWT" });
  // Apple accepts at most 20 minutes.
  const body = b64({ iss: issuerId, iat: now, exp: now + 15 * 60, aud: "appstoreconnect-v1" });
  const input = `${head}.${body}`;
  const sig = crypto.sign("sha256", Buffer.from(input), {
    key: privateKeyPem,
    dsaEncoding: "ieee-p1363",
  });
  return `${input}.${sig.toString("base64url")}`;
}

function normalizePem(text) {
  // GitHub secrets keep newlines, but a key pasted as one line with literal \n also works.
  return text.includes("\\n") && !text.includes("\n") ? text.replace(/\\n/g, "\n") : text;
}

class AscClient {
  constructor({ base, keyId, issuerId, privateKey }) {
    this.base = base.replace(/\/$/, "");
    this.keyId = keyId;
    this.issuerId = issuerId;
    this.privateKey = normalizePem(privateKey);
  }

  async get(urlOrPath) {
    const url = urlOrPath.startsWith("http") ? urlOrPath : this.base + urlOrPath;
    const token = makeJwt(this.keyId, this.issuerId, this.privateKey);
    const res = await fetch(url, { headers: { Authorization: `Bearer ${token}` } });
    if (!res.ok) {
      const text = await res.text();
      let hint = "";
      if (res.status === 401) hint = " (check ASC_KEY_ID, ASC_ISSUER_ID and ASC_PRIVATE_KEY)";
      if (res.status === 403) hint = " (the API key's role may not allow reading TestFlight feedback)";
      throw new Error(`App Store Connect ${res.status}${hint}: ${text.slice(0, 300)}`);
    }
    return res.json();
  }

  // All pages of a list endpoint; included resources merged.
  async list(pathWithQuery) {
    const data = [];
    const included = [];
    let next = pathWithQuery;
    while (next) {
      const page = await this.get(next);
      data.push(...(page.data || []));
      included.push(...(page.included || []));
      next = page.links && page.links.next;
    }
    return { data, included };
  }

  async submissions(appId, kind) {
    const type = kind === "crash" ? "betaFeedbackCrashSubmissions" : "betaFeedbackScreenshotSubmissions";
    const fields = kind === "crash" ? SUBMISSION_FIELDS : [...SUBMISSION_FIELDS, "screenshots"];
    const q = new URLSearchParams({
      [`fields[${type}]`]: fields.join(","),
      include: "build",
      "fields[builds]": "version",
      sort: "-createdDate",
      limit: "200",
    });
    return this.list(`/v1/apps/${encodeURIComponent(appId)}/${type}?${q}`);
  }

  async crashLog(submissionId) {
    const r = await this.get(
      `/v1/betaFeedbackCrashSubmissions/${encodeURIComponent(submissionId)}/crashLog?fields[betaCrashLogs]=logText`,
    );
    return (r.data && r.data.attributes && r.data.attributes.logText) || "";
  }
}

// Same interface, answers from JSON files (tests and dry runs without a key).
class FixtureClient {
  constructor(dir) {
    this.dir = dir;
  }
  read(name) {
    const f = path.join(this.dir, name);
    return fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, "utf8")) : { data: [] };
  }
  async submissions(_appId, kind) {
    const r = this.read(kind === "crash" ? "crash-submissions.json" : "screenshot-submissions.json");
    return { data: r.data || [], included: r.included || [] };
  }
  async crashLog(id) {
    const f = path.join(this.dir, `crashlog-${id}.txt`);
    return fs.existsSync(f) ? fs.readFileSync(f, "utf8") : "";
  }
}

// ---------------------------------------------------------------- crash summary

// Removes things that could identify a device or a person from one line of a crash report.
function scrub(line) {
  return line
    .replace(/\/(?:private\/)?var\/(?:mobile|containers)\/[^\s'")]*/g, "<app path>")
    .replace(/\/Users\/[^\s'")]+/g, "<path>")
    .replace(/[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}/gi, "<uuid>");
}

function summarizeText(log) {
  const lines = log.split(/\r?\n/);
  const pick = (re) => {
    const l = lines.find((x) => re.test(x));
    return l ? l.replace(re, "").trim() : "";
  };
  const exception = pick(/^Exception Type:\s*/);
  const codes = pick(/^Exception Codes:\s*/);
  const reason = pick(/^Termination Reason:\s*/) || pick(/^Exception Reason:\s*/);
  const frames = [];
  const start = lines.findIndex((x) => /^Thread \d+(?: name:.*)? Crashed:/.test(x) || /^Thread \d+ Crashed::/.test(x));
  let thread = "";
  if (start >= 0) {
    thread = lines[start].replace(/:+\s*$/, "").replace(/ Crashed.*/, "");
    for (let i = start + 1; i < lines.length && frames.length < MAX_FRAMES; i++) {
      const l = lines[i].trim();
      if (!l) break;
      if (/^\d+\s/.test(l)) frames.push(l.replace(/\s+/g, " "));
    }
  }
  return { exception, codes, reason, thread, frames };
}

// .ips format: one JSON header line, then a JSON body.
function summarizeIps(log) {
  const nl = log.indexOf("\n");
  const body = JSON.parse(log.slice(nl + 1));
  const ex = body.exception || {};
  const exception = [ex.type, ex.signal ? `(${ex.signal})` : ""].filter(Boolean).join(" ");
  const codes = ex.codes || "";
  const term = body.termination || {};
  const reason = [term.namespace, term.indicator].filter(Boolean).join(" ")
    || (body.asi ? Object.values(body.asi).flat().join(" ") : "");
  const images = body.usedImages || [];
  const idx = typeof body.faultingThread === "number" ? body.faultingThread : 0;
  const t = (body.threads || [])[idx] || {};
  const frames = (t.frames || []).slice(0, MAX_FRAMES).map((f, i) => {
    const img = images[f.imageIndex] || {};
    const name = img.name || "???";
    const where = f.symbol
      ? `${f.symbol} + ${f.symbolLocation ?? 0}`
      : `${name} + ${f.imageOffset ?? "?"}`;
    return `${i} ${name} ${where}`;
  });
  return { exception, codes, reason, thread: `Thread ${idx}`, frames };
}

export function summarizeCrash(log) {
  if (!log || !log.trim()) return null;
  let s;
  try {
    s = log.trimStart().startsWith("{") ? summarizeIps(log.trimStart()) : summarizeText(log);
  } catch {
    s = summarizeText(log);
  }
  const clean = (x) => scrub(String(x || "")).slice(0, 300);
  return {
    exception: clean(s.exception),
    codes: clean(s.codes),
    reason: clean(s.reason),
    thread: clean(s.thread),
    frames: s.frames.map((f) => clean(f)),
  };
}

// ---------------------------------------------------------------- issue text

// Tester text goes into a quote. No @mentions, no HTML (and so no fake marker).
function quoteComment(text) {
  const t = String(text || "").trim();
  if (!t) return "_No comment._";
  return t
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/@/g, "@​")
    .split(/\r?\n/)
    .map((l) => `> ${l}`)
    .join("\n");
}

function oneLine(text, max) {
  const t = String(text || "").replace(/\s+/g, " ").replace(/@/g, "@​").trim();
  return t.length > max ? t.slice(0, max - 1).trimEnd() + "…" : t;
}

function buildInfo(sub, included) {
  const rel = sub.relationships && sub.relationships.build && sub.relationships.build.data;
  if (!rel) return "unknown";
  const b = included.find((x) => x.type === "builds" && x.id === rel.id);
  const version = b && b.attributes && b.attributes.version;
  return version || "unknown";
}

function deviceLine(a) {
  return a.deviceModel || "unknown";
}

function osName(a) {
  const names = { IOS: "iOS", MAC_OS: "macOS", TV_OS: "tvOS", VISION_OS: "visionOS" };
  if (a.devicePlatform === "IOS" && a.deviceFamily === "IPAD") return "iPadOS";
  return names[a.devicePlatform] || a.devicePlatform || "";
}

export function issueFor(kind, sub, included, crashSummary) {
  const a = sub.attributes || {};
  const date = a.createdDate ? a.createdDate.replace(/\.\d+/, "").replace("T", " ").replace(/Z$/, " UTC") : "unknown";
  const build = buildInfo(sub, included);
  const rows = [
    "| | |",
    "|---|---|",
    `| Build | ${build} |`,
    `| Device | ${deviceLine(a)} |`,
    `| OS | ${[osName(a), a.osVersion].filter(Boolean).join(" ") || "unknown"} |`,
    `| Date | ${date} |`,
  ];
  const parts = [];
  let title;
  if (kind === "crash") {
    const what = crashSummary && crashSummary.exception ? crashSummary.exception : "crash";
    title = `TestFlight crash (build ${build}): ${oneLine(a.comment, 60) || what}`;
    parts.push("Crash feedback sent with TestFlight.", "", ...rows, "", "**Comment**", "", quoteComment(a.comment), "");
    parts.push("**Crash summary**", "");
    if (crashSummary) {
      const lines = [];
      if (crashSummary.exception) lines.push(`Exception: ${crashSummary.exception}`);
      if (crashSummary.codes) lines.push(`Codes: ${crashSummary.codes}`);
      if (crashSummary.reason) lines.push(`Termination: ${crashSummary.reason}`);
      if (crashSummary.frames.length) {
        lines.push("", `${crashSummary.thread || "Crashed thread"}, top frames:`, ...crashSummary.frames);
      }
      parts.push("```", ...lines, "```", "");
    } else {
      parts.push("_No crash log available._", "");
    }
    parts.push("The full crash log is available to the maintainer.");
  } else {
    title = `TestFlight feedback (build ${build}): ${oneLine(a.comment, 60) || "screenshot"}`;
    const n = Array.isArray(a.screenshots) ? a.screenshots.length : 0;
    parts.push("Feedback sent with TestFlight.", "", ...rows, "", "**Comment**", "", quoteComment(a.comment), "");
    if (n > 0) parts.push(n === 1 ? "Screenshot available to the maintainer." : `${n} screenshots available to the maintainer.`);
  }
  parts.push("", `<!-- tf-feedback-id: ${sub.id} -->`);
  return { title, body: parts.join("\n"), labels: [LABEL] };
}

// ---------------------------------------------------------------- GitHub

class GitHubClient {
  constructor({ base, token, repo }) {
    this.base = base.replace(/\/$/, "");
    this.token = token;
    this.repo = repo;
  }
  async req(method, p, body) {
    const res = await fetch(this.base + p, {
      method,
      headers: {
        Authorization: `Bearer ${this.token}`,
        Accept: "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "testflight-feedback-sync",
        ...(body ? { "Content-Type": "application/json" } : {}),
      },
      body: body ? JSON.stringify(body) : undefined,
    });
    if (!res.ok && !(method === "GET" && res.status === 404)) {
      throw new Error(`GitHub ${method} ${p}: ${res.status} ${(await res.text()).slice(0, 300)}`);
    }
    return res.status === 404 ? null : res.json();
  }
  async postedIds() {
    const ids = new Set();
    for (let page = 1; ; page++) {
      const list = await this.req("GET", `/repos/${this.repo}/issues?state=all&labels=${LABEL}&per_page=100&page=${page}`);
      if (!list || list.length === 0) break;
      for (const issue of list) for (const m of String(issue.body || "").matchAll(MARKER_RE)) ids.add(m[1]);
      if (list.length < 100) break;
    }
    return ids;
  }
  async ensureLabel() {
    const l = await this.req("GET", `/repos/${this.repo}/labels/${LABEL}`);
    if (!l) {
      await this.req("POST", `/repos/${this.repo}/labels`, {
        name: LABEL,
        color: "0e8a16",
        description: "Feedback sent by testers through TestFlight",
      });
    }
  }
  async create(issue) {
    return this.req("POST", `/repos/${this.repo}/issues`, issue);
  }
}

// ---------------------------------------------------------------- main

function parseArgs(argv) {
  const o = { dryRun: false, fixture: null, max: 20, since: null };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--dry-run") o.dryRun = true;
    else if (a === "--fixture") o.fixture = argv[++i];
    else if (a === "--max") o.max = Number(argv[++i]);
    else if (a === "--since") o.since = argv[++i];
    else if (a === "-h" || a === "--help") o.help = true;
    else throw new Error(`unknown argument: ${a}`);
  }
  if (!Number.isInteger(o.max) || o.max < 0) throw new Error("--max needs a number");
  if (o.since && Number.isNaN(Date.parse(o.since))) throw new Error("--since needs a date (YYYY-MM-DD)");
  return o;
}

export async function run(argv, env = process.env, log = console.log) {
  const opt = parseArgs(argv);
  if (opt.help) {
    log("usage: sync.mjs [--dry-run] [--fixture DIR] [--max N] [--since YYYY-MM-DD]");
    return { created: [] };
  }
  const need = (k) => {
    if (!env[k]) throw new Error(`missing environment variable ${k}`);
    return env[k];
  };

  const asc = opt.fixture
    ? new FixtureClient(opt.fixture)
    : new AscClient({
        base: env.ASC_API_BASE || "https://api.appstoreconnect.apple.com",
        keyId: need("ASC_KEY_ID"),
        issuerId: need("ASC_ISSUER_ID"),
        privateKey: need("ASC_PRIVATE_KEY"),
      });
  const appId = opt.fixture ? env.ASC_APP_ID || "fixture" : need("ASC_APP_ID");

  // GitHub is needed to post, and to skip what is already there. A dry run without a token
  // assumes nothing has been posted yet.
  const gh = env.GITHUB_TOKEN
    ? new GitHubClient({
        base: env.GITHUB_API_URL || "https://api.github.com",
        token: env.GITHUB_TOKEN,
        repo: need("GITHUB_REPOSITORY"),
      })
    : null;
  if (!gh && !opt.dryRun) throw new Error("missing environment variable GITHUB_TOKEN");
  const posted = gh ? await gh.postedIds() : new Set();

  const since = Math.max(Date.parse(START), opt.since ? Date.parse(opt.since) : 0);
  const todo = [];
  for (const kind of ["screenshot", "crash"]) {
    const { data, included } = await asc.submissions(appId, kind);
    for (const sub of data) {
      if (posted.has(sub.id)) continue;
      const t = Date.parse((sub.attributes && sub.attributes.createdDate) || "");
      if (!(t >= since)) continue; // also skips a missing date
      todo.push({ kind, sub, included, t: Number.isNaN(t) ? 0 : t });
    }
  }
  todo.sort((x, y) => x.t - y.t); // oldest first, so issue numbers follow the dates
  log(`feedback since ${new Date(since).toISOString()}: ${todo.length} new, ${posted.size} already posted`);
  if (todo.length > opt.max) log(`limit: posting ${opt.max} now, the rest in a later run`);

  const created = [];
  let labelReady = false;
  for (const item of todo.slice(0, opt.max)) {
    const summary = item.kind === "crash" ? summarizeCrash(await asc.crashLog(item.sub.id)) : null;
    const issue = issueFor(item.kind, item.sub, item.included, summary);
    if (opt.dryRun) {
      log(`\n--- would post: ${issue.title}\n${issue.body}\n`);
      created.push(issue);
      continue;
    }
    if (!labelReady) {
      await gh.ensureLabel();
      labelReady = true;
    }
    const r = await gh.create(issue);
    log(`posted #${r.number}: ${issue.title}`);
    created.push(issue);
  }
  return { created };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  run(process.argv.slice(2)).catch((e) => {
    console.error(`error: ${e.message}`);
    process.exit(1);
  });
}

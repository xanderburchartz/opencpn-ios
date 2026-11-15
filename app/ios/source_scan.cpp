// Bronnen zoeken (Instellingen › Verbindingen › "Zoek bronnen…").
//
// OpenCPN 5.14 zoekt navigatiebronnen in de eerste-start-wizard (gui/src/initwiz):
// USB-serieel, UDP, TCP op de router en ARP-buren (via libdnet), SignalK en GPSD. Die
// wizard draait op iOS niet (net als op Android), en USB-serieel en de ARP-tabel zijn op
// iOS niet bereikbaar. Xander (11 nov 2026): "Ja, allebei" (Datamonitor en bronnen zoeken).
//
// Hier, met dezelfde poorten en herkenning als de wizard:
//  - UDP: luisteren op 10110, 2000 en 1458;
//  - TCP: elke host in het eigen /24-netwerk (wifi) op 10110, 2000, 1456, 1457 en 39150,
//    tegelijk met niet-blokkerende sockets; een open poort wordt kort gelezen;
//  - SignalK: mDNS (FindAllSignalKServers), zoals de wizard, en daarnaast poort 3000
//    met een GET /signalk (het discovery-document; alleen lezen).
// Alleen lezen: er worden geen gegevens of opdrachten verstuurd (alleen de GET hierboven).
// Gevonden verbindingen worden pas na een keuze
// van de gebruiker toegevoegd, en dan alleen als invoer (niet zenden).
// SPDX-License-Identifier: GPL-2.0-or-later

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <wx/app.h>
#include <wx/choicdlg.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/string.h>
#include <wx/window.h>

#include "model/conn_params.h"
#include "model/mdns_query.h"

extern std::vector<ocpn_DNS_record_t> g_sk_servers;  // model/src/mdns_query.cpp

namespace {

const std::vector<uint16_t> kUdpPorts = {10110, 2000, 1458};
const std::vector<uint16_t> kTcpPorts = {10110, 2000, 1456, 1457, 39150};

enum class Kind { kNone, kN0183, kN0183Crc, kN2000 };

// Zelfde regels als FirstUseWizImpl::SeemsN0183 / SeemsN2000 (initwiz/wiz_ui.cpp).
Kind Classify(const std::string& data) {
  if (data.empty()) return Kind::kNone;
  if (data.size() > 4 && data[0] == 0x10 && data[1] == 0x02 &&
      data[data.size() - 1] == 0x03 && data[data.size() - 2] == 0x10)
    return Kind::kN2000;
  static const std::regex yd_raw(
      "[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3} [RT] [0-9A-F]{8}( [0-9A-F]{2})*");
  static const std::regex act_ascii(
      "A[0-9]{6}\\.[0-9]{3} [0-9A-F]{5} [0-9A-F]{5} [0-9A-F]+");
  static const std::regex seasmart(
      "\\$PCDIN,[0-9A-F]{6},[0-9A-F]{8},[0-9A-F]{2},[0-9A-F]+\\*[0-9A-F]{2}");
  static const std::regex miniplex(
      "\\$MXPGN,[0-9A-F]{6},[0-9A-F]{4},[0-9A-F]+\\*[0-9A-F]{2}");
  static const std::regex n0183(".*[\\$!]([a-zA-Z]{5,6})(,.*)");
  static const std::regex n0183crc(".*[\\$!]([a-zA-Z]{5,6})(,.*)(\\*[0-9A-Z]{2})");
  std::stringstream ss(data);
  std::string line;
  Kind found = Kind::kNone;
  while (std::getline(ss, line, '\n')) {
    if (std::regex_search(line, yd_raw) || std::regex_search(line, act_ascii) ||
        std::regex_search(line, seasmart) || std::regex_search(line, miniplex))
      return Kind::kN2000;
    if (found == Kind::kNone && std::regex_search(line, n0183))
      found = std::regex_search(line, n0183crc) ? Kind::kN0183Crc : Kind::kN0183;
  }
  return found;
}

struct Found {
  ConnectionParams params;
  wxString label;
};

// Een verbinding die alleen ontvangt. OpenCPN master (na 5.14) heeft de velden van
// ConnectionParams hernoemd; dit is de enige plek die ze hier invult.
ConnectionParams MakeInputConnection(NetworkProtocol np, DataProtocol proto, bool crc,
                                     const std::string& ip, int port,
                                     const wxString& comment) {
  ConnectionParams p;
#ifdef OCPNIOS_OCPN_MASTER
  p.type = ConnectionType::NETWORK;
  p.net_protocol = np;
  p.data_protocol = proto;
  p.last_data_protocol = proto;
  p.checksum_check = crc;
  p.network_address = ip;
  p.network_port = port;
  p.direction = PortDirection::kInput;
  p.is_enabled = true;
  p.user_comment = comment;
#else
  p.Type = ConnectionType::NETWORK;
  p.NetProtocol = np;
  p.Protocol = proto;
  p.LastDataProtocol = proto;
  p.ChecksumCheck = crc;
  p.NetworkAddress = ip;
  p.NetworkPort = port;
  p.IOSelect = DS_TYPE_INPUT;
  p.bEnabled = true;
  p.UserComment = comment;
#endif
  return p;
}

void AddNetwork(std::vector<Found>& out, NetworkProtocol np, Kind kind,
                const std::string& ip, int port) {
  const wxString proto = kind == Kind::kN2000 ? "NMEA 2000" : "NMEA 0183";
  const wxString comment = np == NetworkProtocol::UDP
                               ? wxString::Format("%s: UDP %d", proto, port)
                               : wxString::Format("%s: %s TCP %d", proto, ip, port);
  out.push_back({MakeInputConnection(np,
                                     kind == Kind::kN2000 ? DataProtocol::PROTO_NMEA2000
                                                          : DataProtocol::PROTO_NMEA0183,
                                     kind == Kind::kN0183Crc, ip, port, comment),
                 comment});
}

// Wacht tot `fds` leesbaar zijn of de tijd om is; verzamelt per fd de ontvangen bytes.
void ReadAll(std::vector<int>& fds, std::map<int, std::string>& data, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (!fds.empty()) {
    const int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                         end - std::chrono::steady_clock::now())
                         .count();
    if (left <= 0) break;
    std::vector<pollfd> p;
    for (int fd : fds) p.push_back({fd, POLLIN, 0});
    if (poll(p.data(), p.size(), std::min(left, 200)) < 0) break;
    for (auto& e : p) {
      if (!(e.revents & (POLLIN | POLLHUP | POLLERR))) continue;
      char buf[2048];
      const ssize_t n = recv(e.fd, buf, sizeof(buf), 0);
      if (n > 0) data[e.fd].append(buf, n);
    }
    if (wxTheApp) wxTheApp->Yield(true);
  }
}

void ScanUdp(std::vector<Found>& out) {
  std::vector<int> fds;
  std::map<int, int> portOf;
  for (uint16_t port : kUdpPorts) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) continue;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    fcntl(fd, F_SETFL, O_NONBLOCK);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (sockaddr*)&a, sizeof(a)) != 0) {
      wxLogMessage("Bronnen zoeken: UDP %d niet te openen (%s)", port, strerror(errno));
      close(fd);
      continue;
    }
    fds.push_back(fd);
    portOf[fd] = port;
  }
  std::map<int, std::string> data;
  std::vector<int> wait = fds;
  ReadAll(wait, data, 2500);
  for (int fd : fds) {
    const Kind k = Classify(data[fd]);
    if (k != Kind::kNone) AddNetwork(out, NetworkProtocol::UDP, k, "0.0.0.0", portOf[fd]);
    close(fd);
  }
}

// Eigen IPv4-adressen met netmasker (wifi/ethernet, geen loopback of tunnels).
std::vector<std::pair<in_addr_t, in_addr_t>> LocalNets() {
  std::vector<std::pair<in_addr_t, in_addr_t>> nets;
  ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) != 0) return nets;
  for (ifaddrs* i = ifs; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_netmask) continue;
    if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
    const std::string name = i->ifa_name;
    if (name.rfind("en", 0) != 0) continue;  // en0 = wifi, en1.. = ethernet/USB
    const in_addr_t ip = ntohl(((sockaddr_in*)i->ifa_addr)->sin_addr.s_addr);
    const in_addr_t mask = ntohl(((sockaddr_in*)i->ifa_netmask)->sin_addr.s_addr);
    nets.push_back({ip, mask});
  }
  freeifaddrs(ifs);
  return nets;
}

std::vector<std::string> ScanHosts() {
  std::vector<std::string> hosts;
  std::set<std::string> seen;
  hosts.push_back("127.0.0.1");
  for (auto [ip, mask] : LocalNets()) {
    // Hooguit een /24 doorzoeken (een groter netwerk: alleen het eigen /24-deel).
    const in_addr_t m = std::max<in_addr_t>(mask, 0xFFFFFF00u);
    const in_addr_t base = ip & m;
    for (in_addr_t h = 1; h < (~m & 0xFFFFFFFFu); h++) {
      const in_addr_t a = base | h;
      if (a == ip) continue;
      in_addr na{htonl(a)};
      const std::string s = inet_ntoa(na);
      if (seen.insert(s).second) hosts.push_back(s);
    }
  }
  return hosts;
}

// Probeert `port` op alle hosts tegelijk (hooguit 1,2 s), stuurt eventueel `request`
// en leest 2 s. Geeft per host de ontvangen bytes terug (alleen hosts met open poort).
std::map<std::string, std::string> ProbePort(const std::vector<std::string>& hosts,
                                             uint16_t port, const std::string& request) {
  std::map<int, std::string> hostOf;
  std::vector<int> pending;
  for (const auto& h : hosts) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) continue;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    fcntl(fd, F_SETFL, O_NONBLOCK);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, h.c_str(), &a.sin_addr);
    if (connect(fd, (sockaddr*)&a, sizeof(a)) != 0 && errno != EINPROGRESS) {
      close(fd);
      continue;
    }
    pending.push_back(fd);
    hostOf[fd] = h;
  }
  std::vector<int> open;
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  while (!pending.empty() && std::chrono::steady_clock::now() < end) {
    std::vector<pollfd> p;
    for (int fd : pending) p.push_back({fd, POLLOUT, 0});
    if (poll(p.data(), p.size(), 100) < 0) break;
    std::vector<int> still;
    for (auto& e : p) {
      if (e.revents & (POLLOUT | POLLERR | POLLHUP)) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(e.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err == 0 && (e.revents & POLLOUT)) open.push_back(e.fd);
        else close(e.fd);
      } else {
        still.push_back(e.fd);
      }
    }
    pending.swap(still);
    if (wxTheApp) wxTheApp->Yield(true);
  }
  for (int fd : pending) close(fd);
  std::map<std::string, std::string> result;
  if (open.empty()) return result;
  if (!request.empty())
    for (int fd : open) send(fd, request.data(), request.size(), 0);
  std::map<int, std::string> data;
  std::vector<int> wait = open;
  ReadAll(wait, data, 2000);
  for (int fd : open) {
    result[hostOf[fd]] = data[fd];
    close(fd);
  }
  return result;
}

void ScanTcp(std::vector<Found>& out, const std::vector<std::string>& hosts,
             wxProgressDialog* prog) {
  for (uint16_t port : kTcpPorts) {
    if (prog) prog->Pulse(wxString::Format(_("TCP port %d..."), port));
    for (const auto& [host, data] : ProbePort(hosts, port, "")) {
      const Kind k = Classify(data);
      wxLogMessage("Bronnen zoeken: TCP %s:%d open, %zu bytes, soort %d", host.c_str(),
                   port, data.size(), (int)k);
      if (k != Kind::kNone) AddNetwork(out, NetworkProtocol::TCP, k, host, port);
    }
  }
}

void AddSignalK(std::vector<Found>& out, const std::string& ip, int port,
                const std::string& name) {
  const wxString comment = name.empty()
                               ? wxString::Format("SignalK: %s:%d", ip, port)
                               : wxString::Format("SignalK: %s (%s:%d)", name, ip, port);
  out.push_back({MakeInputConnection(NetworkProtocol::SIGNALK, DataProtocol::PROTO_SIGNALK,
                                     false, ip, port, comment),
                 comment});
}

void ScanSignalK(std::vector<Found>& out, const std::vector<std::string>& hosts) {
  // mDNS, zoals de wizard (vond in de simulator niets, 11 nov 2026).
  g_sk_servers.clear();
  FindAllSignalKServers(1);
  for (const auto& sks : g_sk_servers)
    AddSignalK(out, sks.ip, std::atoi(sks.port.c_str()), sks.hostname);
  // Daarnaast de standaardpoort 3000: GET /signalk (alleen lezen) en kijken of het
  // antwoord het SignalK-discovery-document is.
  const std::string req = "GET /signalk HTTP/1.0\r\nHost: x\r\n\r\n";
  for (const auto& [host, data] : ProbePort(hosts, 3000, req)) {
    const bool sk = data.find("\"endpoints\"") != std::string::npos &&
                    data.find("signalk") != std::string::npos;
    wxLogMessage("Bronnen zoeken: TCP %s:3000 open, %zu bytes, SignalK %d", host.c_str(),
                 data.size(), (int)sk);
    if (sk) AddSignalK(out, host, 3000, "");
  }
}

bool SameSource(const ConnectionParams& a, const ConnectionParams& b) {
#ifdef OCPNIOS_OCPN_MASTER
  return a.type == b.type && a.net_protocol == b.net_protocol &&
         a.network_port == b.network_port &&
         (a.net_protocol == NetworkProtocol::UDP || a.network_address == b.network_address);
#else
  return a.Type == b.Type && a.NetProtocol == b.NetProtocol &&
         a.NetworkPort == b.NetworkPort &&
         (a.NetProtocol == NetworkProtocol::UDP || a.NetworkAddress == b.NetworkAddress);
#endif
}

}  // namespace

// Zoekt bronnen en laat de gebruiker kiezen. Geeft de gekozen, nieuwe verbindingen terug
// (nog niet actief; de aanroeper voegt ze toe en start ze).
std::vector<ConnectionParams*> ocpnios_ScanSources(
    wxWindow* parent, const std::vector<ConnectionParams*>& existing) {
  std::vector<Found> found;
  {
    wxProgressDialog prog(_("Find sources"),
                          _("Looking for navigation data sources, this may take a while..."),
                          100, parent, wxPD_APP_MODAL | wxPD_AUTO_HIDE);
    prog.Pulse(_("Looking for UDP data feeds..."));
    ScanUdp(found);
    const std::vector<std::string> hosts = ScanHosts();
    prog.Pulse(_("Looking for TCP servers..."));
    ScanTcp(found, hosts, &prog);
    prog.Pulse(_("Looking for Signal K servers..."));
    ScanSignalK(found, hosts);
  }
  // Wat al als verbinding bestaat niet nog eens aanbieden.
  std::vector<Found> fresh;
  for (auto& f : found) {
    bool dup = false;
    for (auto* e : existing)
      if (e && SameSource(*e, f.params)) dup = true;
    for (auto& g : fresh)
      if (SameSource(g.params, f.params)) dup = true;
    if (!dup) fresh.push_back(f);
  }
  wxLogMessage("Bronnen zoeken: %zu gevonden, %zu nieuw", found.size(), fresh.size());
  std::vector<ConnectionParams*> chosen;
  if (fresh.empty()) {
    wxMessageBox(found.empty() ? _("No navigation data sources found.")
                               : _("All sources found are already configured."),
                 _("Find sources"), wxOK, parent);
    return chosen;
  }
  wxArrayString labels;
  for (auto& f : fresh) labels.Add(f.label);
  wxMultiChoiceDialog dlg(parent, _("Select the sources to add (receive only):"),
                          _("Find sources"), labels);
  wxArrayInt all;
  for (size_t i = 0; i < labels.size(); i++) all.Add((int)i);
  dlg.SetSelections(all);
  if (dlg.ShowModal() != wxID_OK) return chosen;
  for (int i : dlg.GetSelections()) chosen.push_back(new ConnectionParams(fresh[i].params));
  return chosen;
}

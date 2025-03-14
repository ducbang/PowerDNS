/*
 * This file is part of PowerDNS or dnsdist.
 * Copyright -- PowerDNS.COM B.V. and its contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * In addition, for the avoidance of any doubt, permission is granted to
 * link this program with OpenSSL and to (re)distribute the binaries
 * produced as the result of such linking.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "dnsdist-dnsparser.hh"
#include "dnsdist-lua-ffi.hh"
#include "dnsdist-mac-address.hh"
#include "dnsdist-lua-network.hh"
#include "dnsdist-lua.hh"
#include "dnsdist-ecs.hh"
#include "dnsdist-rings.hh"
#include "dolog.hh"

uint16_t dnsdist_ffi_dnsquestion_get_qtype(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->qtype;
}

uint16_t dnsdist_ffi_dnsquestion_get_qclass(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->qclass;
}

static void dnsdist_ffi_comboaddress_to_raw(const ComboAddress& ca, const void** addr, size_t* addrSize)
{
  if (ca.isIPv4()) {
    *addr = &ca.sin4.sin_addr.s_addr;
    *addrSize = sizeof(ca.sin4.sin_addr.s_addr);
  }
  else {
    *addr = &ca.sin6.sin6_addr.s6_addr;
    *addrSize = sizeof(ca.sin6.sin6_addr.s6_addr);
  }
}

void dnsdist_ffi_dnsquestion_get_localaddr(const dnsdist_ffi_dnsquestion_t* dq, const void** addr, size_t* addrSize)
{
  dnsdist_ffi_comboaddress_to_raw(*dq->dq->local, addr, addrSize);
}

void dnsdist_ffi_dnsquestion_get_remoteaddr(const dnsdist_ffi_dnsquestion_t* dq, const void** addr, size_t* addrSize)
{
  dnsdist_ffi_comboaddress_to_raw(*dq->dq->remote, addr, addrSize);
}

size_t dnsdist_ffi_dnsquestion_get_mac_addr(const dnsdist_ffi_dnsquestion_t* dq, void* buffer, size_t bufferSize)
{
  if (dq == nullptr) {
    return 0;
  }

  auto ret = dnsdist::MacAddressesCache::get(*dq->dq->remote, reinterpret_cast<unsigned char*>(buffer), bufferSize);
  if (ret != 0) {
    return 0;
  }

  return 6;
}

void dnsdist_ffi_dnsquestion_get_masked_remoteaddr(dnsdist_ffi_dnsquestion_t* dq, const void** addr, size_t* addrSize, uint8_t bits)
{
  dq->maskedRemote = Netmask(*dq->dq->remote, bits).getMaskedNetwork();
  dnsdist_ffi_comboaddress_to_raw(dq->maskedRemote, addr, addrSize);
}

uint16_t dnsdist_ffi_dnsquestion_get_local_port(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->local->getPort();
}

uint16_t dnsdist_ffi_dnsquestion_get_remote_port(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->remote->getPort();
}

void dnsdist_ffi_dnsquestion_get_qname_raw(const dnsdist_ffi_dnsquestion_t* dq, const char** qname, size_t* qnameSize)
{
  const auto& storage = dq->dq->qname->getStorage();
  *qname = storage.data();
  *qnameSize = storage.size();
}

size_t dnsdist_ffi_dnsquestion_get_qname_hash(const dnsdist_ffi_dnsquestion_t* dq, size_t init)
{
  return dq->dq->qname->hash(init);
}

int dnsdist_ffi_dnsquestion_get_rcode(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->getHeader()->rcode;
}

void* dnsdist_ffi_dnsquestion_get_header(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->getHeader();
}

uint16_t dnsdist_ffi_dnsquestion_get_len(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->getData().size();
}

size_t dnsdist_ffi_dnsquestion_get_size(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->getData().size();
}

bool dnsdist_ffi_dnsquestion_set_size(dnsdist_ffi_dnsquestion_t* dq, size_t newSize)
{
  try {
    dq->dq->getMutableData().resize(newSize);
    return true;
  }
  catch (const std::exception& e) {
    return false;
  }
}

uint8_t dnsdist_ffi_dnsquestion_get_opcode(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->getHeader()->opcode;
}

bool dnsdist_ffi_dnsquestion_get_tcp(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->overTCP();
}

dnsdist_ffi_protocol_type dnsdist_ffi_dnsquestion_get_protocol(const dnsdist_ffi_dnsquestion_t* dq)
{
  if (dq != nullptr) {
    auto proto = dq->dq->getProtocol();
    if (proto == dnsdist::Protocol::DoUDP) {
      return dnsdist_ffi_protocol_type_doudp;
    }
    else if (proto == dnsdist::Protocol::DoTCP) {
      return dnsdist_ffi_protocol_type_dotcp;
    }
    else if (proto == dnsdist::Protocol::DNSCryptUDP) {
      return dnsdist_ffi_protocol_type_dnscryptudp;
    }
    else if (proto == dnsdist::Protocol::DNSCryptTCP) {
      return dnsdist_ffi_protocol_type_dnscrypttcp;
    }
    else if (proto == dnsdist::Protocol::DoT) {
      return dnsdist_ffi_protocol_type_dot;
    }
    else if (proto == dnsdist::Protocol::DoH) {
      return dnsdist_ffi_protocol_type_doh;
    }
  }
  return dnsdist_ffi_protocol_type_doudp;
}

bool dnsdist_ffi_dnsquestion_get_skip_cache(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->skipCache;
}

bool dnsdist_ffi_dnsquestion_get_use_ecs(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->useECS;
}

bool dnsdist_ffi_dnsquestion_get_add_xpf(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->addXPF;
}

bool dnsdist_ffi_dnsquestion_get_ecs_override(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->ecsOverride;
}

uint16_t dnsdist_ffi_dnsquestion_get_ecs_prefix_length(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->ecsPrefixLength;
}

bool dnsdist_ffi_dnsquestion_is_temp_failure_ttl_set(const dnsdist_ffi_dnsquestion_t* dq)
{
  return dq->dq->tempFailureTTL != boost::none;
}

uint32_t dnsdist_ffi_dnsquestion_get_temp_failure_ttl(const dnsdist_ffi_dnsquestion_t* dq)
{
  if (dq->dq->tempFailureTTL) {
    return *dq->dq->tempFailureTTL;
  }
  return 0;
}

bool dnsdist_ffi_dnsquestion_get_do(const dnsdist_ffi_dnsquestion_t* dq)
{
  return getEDNSZ(*dq->dq) & EDNS_HEADER_FLAG_DO;
}

void dnsdist_ffi_dnsquestion_get_sni(const dnsdist_ffi_dnsquestion_t* dq, const char** sni, size_t* sniSize)
{
  *sniSize = dq->dq->sni.size();
  *sni = dq->dq->sni.c_str();
}

const char* dnsdist_ffi_dnsquestion_get_tag(const dnsdist_ffi_dnsquestion_t* dq, const char* label)
{
  const char * result = nullptr;

  if (dq->dq->qTag != nullptr) {
    const auto it = dq->dq->qTag->find(label);
    if (it != dq->dq->qTag->cend()) {
      result = it->second.c_str();
    }
  }

  return result;
}

const char* dnsdist_ffi_dnsquestion_get_http_path(dnsdist_ffi_dnsquestion_t* dq)
{
  if (!dq->httpPath) {
    if (dq->dq->du == nullptr) {
      return nullptr;
    }
#ifdef HAVE_DNS_OVER_HTTPS
    dq->httpPath = dq->dq->du->getHTTPPath();
#endif /* HAVE_DNS_OVER_HTTPS */
  }
  if (dq->httpPath) {
    return dq->httpPath->c_str();
  }
  return nullptr;
}

const char* dnsdist_ffi_dnsquestion_get_http_query_string(dnsdist_ffi_dnsquestion_t* dq)
{
  if (!dq->httpQueryString) {
    if (dq->dq->du == nullptr) {
      return nullptr;
    }
#ifdef HAVE_DNS_OVER_HTTPS
    dq->httpQueryString = dq->dq->du->getHTTPQueryString();
#endif /* HAVE_DNS_OVER_HTTPS */
  }
  if (dq->httpQueryString) {
    return dq->httpQueryString->c_str();
  }
  return nullptr;
}

const char* dnsdist_ffi_dnsquestion_get_http_host(dnsdist_ffi_dnsquestion_t* dq)
{
  if (!dq->httpHost) {
    if (dq->dq->du == nullptr) {
      return nullptr;
    }
#ifdef HAVE_DNS_OVER_HTTPS
    dq->httpHost = dq->dq->du->getHTTPHost();
#endif /* HAVE_DNS_OVER_HTTPS */
  }
  if (dq->httpHost) {
    return dq->httpHost->c_str();
  }
  return nullptr;
}

const char* dnsdist_ffi_dnsquestion_get_http_scheme(dnsdist_ffi_dnsquestion_t* dq)
{
  if (!dq->httpScheme) {
    if (dq->dq->du == nullptr) {
      return nullptr;
    }
#ifdef HAVE_DNS_OVER_HTTPS
    dq->httpScheme = dq->dq->du->getHTTPScheme();
#endif /* HAVE_DNS_OVER_HTTPS */
  }
  if (dq->httpScheme) {
    return dq->httpScheme->c_str();
  }
  return nullptr;
}

static void fill_edns_option(const EDNSOptionViewValue& value, dnsdist_ffi_ednsoption_t& option)
{
  option.len = value.size;
  option.data = nullptr;

  if (value.size > 0) {
    option.data = value.content;
  }
}

// returns the length of the resulting 'out' array. 'out' is not set if the length is 0
size_t dnsdist_ffi_dnsquestion_get_edns_options(dnsdist_ffi_dnsquestion_t* dq, const dnsdist_ffi_ednsoption_t** out)
{
  if (dq->dq->ednsOptions == nullptr) {
    parseEDNSOptions(*(dq->dq));

    if (dq->dq->ednsOptions == nullptr) {
      return 0;
    }
  }

  size_t totalCount = 0;
  for (const auto& option : *dq->dq->ednsOptions) {
    totalCount += option.second.values.size();
  }

  if (!dq->ednsOptionsVect) {
    dq->ednsOptionsVect = std::make_unique<std::vector<dnsdist_ffi_ednsoption_t>>();
  }
  dq->ednsOptionsVect->clear();
  dq->ednsOptionsVect->resize(totalCount);
  size_t pos = 0;
  for (const auto& option : *dq->dq->ednsOptions) {
    for (const auto& entry : option.second.values) {
      fill_edns_option(entry, dq->ednsOptionsVect->at(pos));
      dq->ednsOptionsVect->at(pos).optionCode = option.first;
      pos++;
    }
  }

  if (totalCount > 0) {
    *out = dq->ednsOptionsVect->data();
  }

  return totalCount;
}

size_t dnsdist_ffi_dnsquestion_get_http_headers(dnsdist_ffi_dnsquestion_t* dq, const dnsdist_ffi_http_header_t** out)
{
  if (dq->dq->du == nullptr) {
    return 0;
  }

#ifdef HAVE_DNS_OVER_HTTPS
  auto headers = dq->dq->du->getHTTPHeaders();
  if (headers.size() == 0) {
    return 0;
  }
  dq->httpHeaders = std::make_unique<std::unordered_map<std::string, std::string>>(std::move(headers));
  if (!dq->httpHeadersVect) {
    dq->httpHeadersVect = std::make_unique<std::vector<dnsdist_ffi_http_header_t>>();
  }
  dq->httpHeadersVect->clear();
  dq->httpHeadersVect->resize(dq->httpHeaders->size());
  size_t pos = 0;
  for (const auto& header : *dq->httpHeaders) {
    dq->httpHeadersVect->at(pos).name = header.first.c_str();
    dq->httpHeadersVect->at(pos).value = header.second.c_str();
    ++pos;
  }

  if (!dq->httpHeadersVect->empty()) {
    *out = dq->httpHeadersVect->data();
  }

  return dq->httpHeadersVect->size();
#else
  return 0;
#endif
}

size_t dnsdist_ffi_dnsquestion_get_tag_array(dnsdist_ffi_dnsquestion_t* dq, const dnsdist_ffi_tag_t** out)
{
  if (dq->dq->qTag == nullptr || dq->dq->qTag->size() == 0) {
    return 0;
  }

  if (!dq->tagsVect) {
    dq->tagsVect = std::make_unique<std::vector<dnsdist_ffi_tag_t>>();
  }
  dq->tagsVect->clear();
  dq->tagsVect->resize(dq->dq->qTag->size());
  size_t pos = 0;

  for (const auto& tag : *dq->dq->qTag) {
    auto& entry = dq->tagsVect->at(pos);
    entry.name = tag.first.c_str();
    entry.value = tag.second.c_str();
    ++pos;
  }


  if (!dq->tagsVect->empty()) {
    *out = dq->tagsVect->data();
  }

  return dq->tagsVect->size();
}

void dnsdist_ffi_dnsquestion_set_result(dnsdist_ffi_dnsquestion_t* dq, const char* str, size_t strSize)
{
  dq->result = std::string(str, strSize);
}

void dnsdist_ffi_dnsquestion_set_http_response(dnsdist_ffi_dnsquestion_t* dq, uint16_t statusCode, const char* body, size_t bodyLen, const char* contentType)
{
  if (dq->dq->du == nullptr) {
    return;
  }

#ifdef HAVE_DNS_OVER_HTTPS
  PacketBuffer bodyVect(body, body + bodyLen);
  dq->dq->du->setHTTPResponse(statusCode, std::move(bodyVect), contentType);
  dq->dq->getHeader()->qr = true;
#endif
}

void dnsdist_ffi_dnsquestion_set_rcode(dnsdist_ffi_dnsquestion_t* dq, int rcode)
{
  dq->dq->getHeader()->rcode = rcode;
  dq->dq->getHeader()->qr = true;
}

void dnsdist_ffi_dnsquestion_set_len(dnsdist_ffi_dnsquestion_t* dq, uint16_t len)
{
  dq->dq->getMutableData().resize(len);
}

void dnsdist_ffi_dnsquestion_set_skip_cache(dnsdist_ffi_dnsquestion_t* dq, bool skipCache)
{
  dq->dq->skipCache = skipCache;
}

void dnsdist_ffi_dnsquestion_set_use_ecs(dnsdist_ffi_dnsquestion_t* dq, bool useECS)
{
  dq->dq->useECS = useECS;
}

void dnsdist_ffi_dnsquestion_set_ecs_override(dnsdist_ffi_dnsquestion_t* dq, bool ecsOverride)
{
  dq->dq->ecsOverride = ecsOverride;
}

void dnsdist_ffi_dnsquestion_set_ecs_prefix_length(dnsdist_ffi_dnsquestion_t* dq, uint16_t ecsPrefixLength)
{
  dq->dq->ecsPrefixLength = ecsPrefixLength;
}

void dnsdist_ffi_dnsquestion_set_temp_failure_ttl(dnsdist_ffi_dnsquestion_t* dq, uint32_t tempFailureTTL)
{
  dq->dq->tempFailureTTL = tempFailureTTL;
}

void dnsdist_ffi_dnsquestion_unset_temp_failure_ttl(dnsdist_ffi_dnsquestion_t* dq)
{
  dq->dq->tempFailureTTL = boost::none;
}

void dnsdist_ffi_dnsquestion_set_tag(dnsdist_ffi_dnsquestion_t* dq, const char* label, const char* value)
{
  dq->dq->setTag(label, value);
}

size_t dnsdist_ffi_dnsquestion_get_trailing_data(dnsdist_ffi_dnsquestion_t* dq, const char** out)
{
  dq->trailingData = dq->dq->getTrailingData();
  if (!dq->trailingData.empty()) {
    *out = dq->trailingData.data();
  }

  return dq->trailingData.size();
}

bool dnsdist_ffi_dnsquestion_set_trailing_data(dnsdist_ffi_dnsquestion_t* dq, const char* data, size_t dataLen)
{
  return dq->dq->setTrailingData(std::string(data, dataLen));
}

void dnsdist_ffi_dnsquestion_send_trap(dnsdist_ffi_dnsquestion_t* dq, const char* reason, size_t reasonLen)
{
  if (g_snmpAgent && g_snmpTrapsEnabled) {
    g_snmpAgent->sendDNSTrap(*dq->dq, std::string(reason, reasonLen));
  }
}

void dnsdist_ffi_dnsquestion_spoof_packet(dnsdist_ffi_dnsquestion_t* dq, const char* raw, size_t len)
{
  std::string result;
  SpoofAction sa(raw, len);
  sa(dq->dq, &result);
}

void dnsdist_ffi_dnsquestion_spoof_raw(dnsdist_ffi_dnsquestion_t* dq, const dnsdist_ffi_raw_value_t* values, size_t valuesCount)
{
  std::vector<std::string> data;
  data.reserve(valuesCount);

  for (size_t idx = 0; idx < valuesCount; idx++) {
    data.emplace_back(values[idx].value, values[idx].size);
  }

  std::string result;
  SpoofAction sa(data);
  sa(dq->dq, &result);
}

void dnsdist_ffi_dnsquestion_spoof_addrs(dnsdist_ffi_dnsquestion_t* dq, const dnsdist_ffi_raw_value_t* values, size_t valuesCount)
{
  std::vector<ComboAddress> data;
  data.reserve(valuesCount);

  for (size_t idx = 0; idx < valuesCount; idx++) {
    if (values[idx].size == 4) {
      sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = 0;
      memcpy(&sin.sin_addr.s_addr, values[idx].value, sizeof(sin.sin_addr.s_addr));
      data.emplace_back(&sin);
    }
    else if (values[idx].size == 16) {
      sockaddr_in6 sin6;
      sin6.sin6_family = AF_INET6;
      sin6.sin6_port = 0;
      sin6.sin6_scope_id = 0;
      sin6.sin6_flowinfo = 0;
      memcpy(&sin6.sin6_addr.s6_addr, values[idx].value, sizeof(sin6.sin6_addr.s6_addr));
      data.emplace_back(&sin6);
    }
  }

  std::string result;
  SpoofAction sa(data);
  sa(dq->dq, &result);
}

size_t dnsdist_ffi_servers_list_get_count(const dnsdist_ffi_servers_list_t* list)
{
  return list->ffiServers.size();
}

void dnsdist_ffi_servers_list_get_server(const dnsdist_ffi_servers_list_t* list, size_t idx, const dnsdist_ffi_server_t** out)
{
  *out = &list->ffiServers.at(idx);
}

static size_t dnsdist_ffi_servers_get_index_from_server(const ServerPolicy::NumberedServerVector& servers, const std::shared_ptr<DownstreamState>& server)
{
  for (const auto& pair : servers) {
    if (pair.second == server) {
      return pair.first - 1;
    }
  }
  throw std::runtime_error("Unable to find servers in server list");
}

size_t dnsdist_ffi_servers_list_chashed(const dnsdist_ffi_servers_list_t* list, const dnsdist_ffi_dnsquestion_t* dq, size_t hash)
{
  auto server = chashedFromHash(list->servers, hash);
  return dnsdist_ffi_servers_get_index_from_server(list->servers, server);
}

size_t dnsdist_ffi_servers_list_whashed(const dnsdist_ffi_servers_list_t* list, const dnsdist_ffi_dnsquestion_t* dq, size_t hash)
{
  auto server = whashedFromHash(list->servers, hash);
  return dnsdist_ffi_servers_get_index_from_server(list->servers, server);
}

uint64_t dnsdist_ffi_server_get_outstanding(const dnsdist_ffi_server_t* server)
{
  return server->server->outstanding;
}

int dnsdist_ffi_server_get_weight(const dnsdist_ffi_server_t* server)
{
  return server->server->d_config.d_weight;
}

int dnsdist_ffi_server_get_order(const dnsdist_ffi_server_t* server)
{
  return server->server->d_config.order;
}

double dnsdist_ffi_server_get_latency(const dnsdist_ffi_server_t* server)
{
  return server->server->latencyUsec;
}

bool dnsdist_ffi_server_is_up(const dnsdist_ffi_server_t* server)
{
  return server->server->isUp();
}

const char* dnsdist_ffi_server_get_name(const dnsdist_ffi_server_t* server)
{
  return server->server->getName().c_str();
}

const char* dnsdist_ffi_server_get_name_with_addr(const dnsdist_ffi_server_t* server)
{
  return server->server->getNameWithAddr().c_str();
}

void dnsdist_ffi_dnsresponse_set_min_ttl(dnsdist_ffi_dnsresponse_t* dr, uint32_t min)
{
  dnsdist_ffi_dnsresponse_limit_ttl(dr, min, std::numeric_limits<uint32_t>::max());
}

void dnsdist_ffi_dnsresponse_set_max_ttl(dnsdist_ffi_dnsresponse_t* dr, uint32_t max)
{
  dnsdist_ffi_dnsresponse_limit_ttl(dr, 0, max);
}

void dnsdist_ffi_dnsresponse_limit_ttl(dnsdist_ffi_dnsresponse_t* dr, uint32_t min, uint32_t max)
{
  if (dr->dr != nullptr) {
    std::string result;
    LimitTTLResponseAction ac(min, max);
    ac(dr->dr, &result);
  }
}

void dnsdist_ffi_dnsresponse_clear_records_type(dnsdist_ffi_dnsresponse_t* dr, uint16_t qtype)
{
  if (dr->dr != nullptr) {
    clearDNSPacketRecordTypes(dr->dr->getMutableData(), std::set<QType>{qtype});
  }
}

static constexpr char s_lua_ffi_code[] = R"FFICodeContent(
  local ffi = require("ffi")
  local C = ffi.C

  ffi.cdef[[
)FFICodeContent"
#include "dnsdist-lua-ffi-interface.inc"
R"FFICodeContent(
  ]]

)FFICodeContent";

const char* getLuaFFIWrappers()
{
  return s_lua_ffi_code;
}

void setupLuaLoadBalancingContext(LuaContext& luaCtx)
{
  setupLuaBindings(luaCtx, true);
  setupLuaBindingsDNSQuestion(luaCtx);
  setupLuaBindingsKVS(luaCtx, true);
  setupLuaVars(luaCtx);

#ifdef LUAJIT_VERSION
  luaCtx.executeCode(getLuaFFIWrappers());
#endif
}

void setupLuaFFIPerThreadContext(LuaContext& luaCtx)
{
  setupLuaVars(luaCtx);

#ifdef LUAJIT_VERSION
  luaCtx.executeCode(getLuaFFIWrappers());
#endif
}

size_t dnsdist_ffi_generate_proxy_protocol_payload(const size_t addrSize, const void* srcAddr, const void* dstAddr, const uint16_t srcPort, const uint16_t dstPort, const bool tcp, const size_t valuesCount, const dnsdist_ffi_proxy_protocol_value* values, void* out, const size_t outSize)
{
  try {
    ComboAddress src, dst;
    if (addrSize != sizeof(src.sin4.sin_addr) && addrSize != sizeof(src.sin6.sin6_addr.s6_addr)) {
      return 0;
    }

    src = makeComboAddressFromRaw(addrSize == sizeof(src.sin4.sin_addr) ? 4 : 6, reinterpret_cast<const char*>(srcAddr), addrSize);
    src.sin4.sin_port = htons(srcPort);
    dst = makeComboAddressFromRaw(addrSize == sizeof(dst.sin4.sin_addr) ? 4 : 6, reinterpret_cast<const char*>(dstAddr), addrSize);
    dst.sin4.sin_port = htons(dstPort);

    std::vector<ProxyProtocolValue> valuesVect;
    if (valuesCount > 0) {
      valuesVect.reserve(valuesCount);
      for (size_t idx = 0; idx < valuesCount; idx++) {
        valuesVect.push_back({ std::string(values[idx].value, values[idx].size), values[idx].type });
      }
    }

    std::string payload = makeProxyHeader(tcp, src, dst, valuesVect);
    if (payload.size() > outSize) {
      return 0;
    }

    memcpy(out, payload.c_str(), payload.size());

    return payload.size();
  }
  catch (const std::exception& e) {
    vinfolog("Exception in dnsdist_ffi_generate_proxy_protocol_payload: %s", e.what());
    return 0;
  }
  catch (...) {
    vinfolog("Unhandled exception in dnsdist_ffi_generate_proxy_protocol_payload");
    return 0;
  }
}

size_t dnsdist_ffi_dnsquestion_generate_proxy_protocol_payload(const dnsdist_ffi_dnsquestion_t* dq, const size_t valuesCount, const dnsdist_ffi_proxy_protocol_value* values, void* out, const size_t outSize)
{
  std::vector<ProxyProtocolValue> valuesVect;
  if (valuesCount > 0) {
    valuesVect.reserve(valuesCount);
    for (size_t idx = 0; idx < valuesCount; idx++) {
      valuesVect.push_back({ std::string(values[idx].value, values[idx].size), values[idx].type });
    }
  }

  std::string payload = makeProxyHeader(dq->dq->overTCP(), *dq->dq->remote, *dq->dq->local, valuesVect);
  if (payload.size() > outSize) {
    return 0;
  }

  memcpy(out, payload.c_str(), payload.size());

  return payload.size();
}

struct dnsdist_ffi_domain_list_t
{
  std::vector<std::string> d_domains;
};
struct dnsdist_ffi_address_list_t {
  std::vector<std::string> d_addresses;
};

const char* dnsdist_ffi_domain_list_get(const dnsdist_ffi_domain_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_domains.size()) {
    return nullptr;
  }

  return list->d_domains.at(idx).c_str();
}

void dnsdist_ffi_domain_list_free(dnsdist_ffi_domain_list_t* list)
{
  delete list;
}

const char* dnsdist_ffi_address_list_get(const dnsdist_ffi_address_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_addresses.size()) {
    return nullptr;
  }

  return list->d_addresses.at(idx).c_str();
}

void dnsdist_ffi_address_list_free(dnsdist_ffi_address_list_t* list)
{
  delete list;
}

size_t dnsdist_ffi_packetcache_get_domain_list_by_addr(const char* poolName, const char* addr, dnsdist_ffi_domain_list_t** out)
{
  if (poolName == nullptr || addr == nullptr || out == nullptr) {
    return 0;
  }

  ComboAddress ca;
  try {
    ca = ComboAddress(addr);
  }
  catch (const std::exception& e) {
    vinfolog("Error parsing address passed to dnsdist_ffi_packetcache_get_domain_list_by_addr: %s", e.what());
    return 0;
  }

  const auto localPools = g_pools.getCopy();
  auto it = localPools.find(poolName);
  if (it == localPools.end()) {
    return 0;
  }

  auto pool = it->second;
  if (!pool->packetCache) {
    return 0;
  }

  auto domains = pool->packetCache->getDomainsContainingRecords(ca);
  if (domains.size() == 0) {
    return 0;
  }

  auto list = std::make_unique<dnsdist_ffi_domain_list_t>();
  list->d_domains.reserve(domains.size());
  for (const auto& domain : domains) {
    try {
      list->d_domains.push_back(domain.toString());
    }
    catch (const std::exception& e) {
      vinfolog("Error converting domain to string in dnsdist_ffi_packetcache_get_domain_list_by_addr: %s", e.what());
    }
  }

  size_t count = list->d_domains.size();
  if (count > 0) {
    *out = list.release();
  }
  return count;
}

size_t dnsdist_ffi_packetcache_get_address_list_by_domain(const char* poolName, const char* domain, dnsdist_ffi_address_list_t** out)
{
  if (poolName == nullptr || domain == nullptr || out == nullptr) {
    return 0;
  }

  DNSName name;
  try {
    name = DNSName(domain);
  }
  catch (const std::exception& e) {
    vinfolog("Error parsing domain passed to dnsdist_ffi_packetcache_get_address_list_by_domain: %s", e.what());
    return 0;
  }

  const auto localPools = g_pools.getCopy();
  auto it = localPools.find(poolName);
  if (it == localPools.end()) {
    return 0;
  }

  auto pool = it->second;
  if (!pool->packetCache) {
    return 0;
  }

  auto addresses = pool->packetCache->getRecordsForDomain(name);
  if (addresses.size() == 0) {
    return 0;
  }

  auto list = std::make_unique<dnsdist_ffi_address_list_t>();
  list->d_addresses.reserve(addresses.size());
  for (const auto& addr : addresses) {
    try {
      list->d_addresses.push_back(addr.toString());
    }
    catch (const std::exception& e) {
      vinfolog("Error converting address to string in dnsdist_ffi_packetcache_get_address_list_by_domain: %s", e.what());
    }
  }

  size_t count = list->d_addresses.size();
  if (count > 0) {
    *out = list.release();
  }
  return count;
}

struct dnsdist_ffi_ring_entry_list_t
{
  struct entry
  {
    std::string qname;
    std::string requestor;
    std::string macAddr;
    size_t size;
    uint16_t qtype;
    dnsdist::Protocol protocol;
    bool isResponse;
  };

  std::vector<entry> d_entries;
};

bool dnsdist_ffi_ring_entry_is_response(const dnsdist_ffi_ring_entry_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_entries.size()) {
    return false;
  }

  return list->d_entries.at(idx).isResponse;
}

const char* dnsdist_ffi_ring_entry_get_name(const dnsdist_ffi_ring_entry_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_entries.size()) {
    return nullptr;
  }

  return list->d_entries.at(idx).qname.c_str();
}

uint16_t dnsdist_ffi_ring_entry_get_type(const dnsdist_ffi_ring_entry_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_entries.size()) {
    return 0;
  }

  return list->d_entries.at(idx).qtype;

}

const char* dnsdist_ffi_ring_entry_get_requestor(const dnsdist_ffi_ring_entry_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_entries.size()) {
    return nullptr;
  }

  return list->d_entries.at(idx).requestor.c_str();
}

uint8_t dnsdist_ffi_ring_entry_get_protocol(const dnsdist_ffi_ring_entry_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_entries.size()) {
    return 0;
  }

  return list->d_entries.at(idx).protocol.toNumber();
}

uint16_t dnsdist_ffi_ring_entry_get_size(const dnsdist_ffi_ring_entry_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_entries.size()) {
    return 0;
  }

  return list->d_entries.at(idx).size;

}

bool dnsdist_ffi_ring_entry_has_mac_address(const dnsdist_ffi_ring_entry_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_entries.size()) {
    return false;
  }

  return !list->d_entries.at(idx).macAddr.empty();
}

const char* dnsdist_ffi_ring_entry_get_mac_address(const dnsdist_ffi_ring_entry_list_t* list, size_t idx)
{
  if (list == nullptr || idx >= list->d_entries.size()) {
    return nullptr;
  }

  return list->d_entries.at(idx).macAddr.data();

}

void dnsdist_ffi_ring_entry_list_free(dnsdist_ffi_ring_entry_list_t* list)
{
  delete list;
}

template<typename T> static void addRingEntryToList(std::unique_ptr<dnsdist_ffi_ring_entry_list_t>& list, const T& entry)
{
  constexpr bool response = std::is_same_v<T, Rings::Response>;
#if defined(DNSDIST_RINGS_WITH_MACADDRESS)
  if constexpr (!response) {
    dnsdist_ffi_ring_entry_list_t::entry tmp{entry.name.toString(), entry.requestor.toString(), entry.hasmac ? std::string(reinterpret_cast<const char*>(entry.macaddress.data()), entry.macaddress.size()) : std::string(), entry.size, entry.qtype, entry.protocol, response};
    list->d_entries.push_back(std::move(tmp));
  }
  else {
    dnsdist_ffi_ring_entry_list_t::entry tmp{entry.name.toString(), entry.requestor.toString(), std::string(), entry.size, entry.qtype, entry.protocol, response};
    list->d_entries.push_back(std::move(tmp));
  }
#else
  dnsdist_ffi_ring_entry_list_t::entry tmp{entry.name.toString(), entry.requestor.toString(), std::string(), entry.size, entry.qtype, entry.protocol, response};
  list->d_entries.push_back(std::move(tmp));
#endif
}

size_t dnsdist_ffi_ring_get_entries(dnsdist_ffi_ring_entry_list_t** out)
{
  if (out == nullptr) {
    return 0;
  }
  auto list = std::make_unique<dnsdist_ffi_ring_entry_list_t>();

  for (const auto& shard : g_rings.d_shards) {
    {
      auto ql = shard->queryRing.lock();
      for (const auto& entry : *ql) {
        addRingEntryToList(list, entry);
      }
    }
    {
      auto rl = shard->respRing.lock();
      for (const auto& entry : *rl) {
        addRingEntryToList(list, entry);
      }
    }
  }

  auto count = list->d_entries.size();
  if (count > 0) {
    *out = list.release();
  }
  return count;
}

size_t dnsdist_ffi_ring_get_entries_by_addr(const char* addr, dnsdist_ffi_ring_entry_list_t** out)
{
  if (out == nullptr || addr == nullptr) {
    return 0;
  }
  ComboAddress ca;
  try {
    ca = ComboAddress(addr);
  }
  catch (const std::exception& e) {
    vinfolog("Unable to convert address in dnsdist_ffi_ring_get_entries_by_addr: %s", e.what());
    return 0;
  }

  auto list = std::make_unique<dnsdist_ffi_ring_entry_list_t>();

  auto compare = ComboAddress::addressOnlyEqual();
  for (const auto& shard : g_rings.d_shards) {
    {
      auto ql = shard->queryRing.lock();
      for (const auto& entry : *ql) {
        if (!compare(entry.requestor, ca)) {
          continue;
        }

        addRingEntryToList(list, entry);
      }
    }
    {
      auto rl = shard->respRing.lock();
      for (const auto& entry : *rl) {
        if (!compare(entry.requestor, ca)) {
          continue;
        }

        addRingEntryToList(list, entry);
      }
    }
  }

  auto count = list->d_entries.size();
  if (count > 0) {
    *out = list.release();
  }
  return count;
}

size_t dnsdist_ffi_ring_get_entries_by_mac(const char* addr, dnsdist_ffi_ring_entry_list_t** out)
{
  if (out == nullptr || addr == nullptr) {
    return 0;
  }

#if !defined(DNSDIST_RINGS_WITH_MACADDRESS)
  return 0;
#else
  auto list = std::make_unique<dnsdist_ffi_ring_entry_list_t>();

  for (const auto& shard : g_rings.d_shards) {
    auto ql = shard->queryRing.lock();
    for (const auto& entry : *ql) {
      if (memcmp(addr, entry.macaddress.data(), entry.macaddress.size()) != 0) {
        continue;
      }

      addRingEntryToList(list, entry);
    }
  }

  auto count = list->d_entries.size();
  if (count > 0) {
    *out = list.release();
  }
  return count;
#endif
}

struct dnsdist_ffi_network_endpoint_t
{
  dnsdist::NetworkEndpoint d_endpoint;
};

bool dnsdist_ffi_network_endpoint_new(const char* path, size_t pathSize, dnsdist_ffi_network_endpoint_t** out)
{
  if (path == nullptr || pathSize == 0 || out == nullptr) {
    return false;
  }
  try {
    dnsdist::NetworkEndpoint endpoint(std::string(path, pathSize));
    *out = new dnsdist_ffi_network_endpoint_t{std::move(endpoint)};
    return true;
  }
  catch (const std::exception& e) {
    vinfolog("Error creating a new network endpoint: %s", e.what());
    return false;
  }
}

bool dnsdist_ffi_network_endpoint_is_valid(const dnsdist_ffi_network_endpoint_t* endpoint)
{
  return endpoint != nullptr;
}

bool dnsdist_ffi_network_endpoint_send(const dnsdist_ffi_network_endpoint_t* endpoint, const char* payload, size_t payloadSize)
{
  if (endpoint != nullptr && payload != nullptr && payloadSize != 0) {
    return endpoint->d_endpoint.send(std::string_view(payload, payloadSize));
  }
  return false;
}

void dnsdist_ffi_network_endpoint_free(dnsdist_ffi_network_endpoint_t* endpoint)
{
  delete endpoint;
}

struct dnsdist_ffi_dnspacket_t
{
  dnsdist::DNSPacketOverlay overlay;
};

bool dnsdist_ffi_dnspacket_parse(const char* packet, size_t packetSize, dnsdist_ffi_dnspacket_t** out)
{
  if (packet == nullptr || out == nullptr || packetSize < sizeof(dnsheader)) {
    return false;
  }

  try {
    dnsdist::DNSPacketOverlay overlay(std::string_view(packet, packetSize));
    *out = new dnsdist_ffi_dnspacket_t{std::move(overlay)};
    return true;
  }
  catch (const std::exception& e) {
    vinfolog("Error in dnsdist_ffi_dnspacket_parse: %s", e.what());
  }
  catch (...) {
    vinfolog("Error in dnsdist_ffi_dnspacket_parse");
  }
  return false;
}

void dnsdist_ffi_dnspacket_get_qname_raw(const dnsdist_ffi_dnspacket_t* packet, const char** qname, size_t* qnameSize)
{
  if (packet == nullptr || qname == nullptr || qnameSize == nullptr) {
    return;
  }
  const auto& storage = packet->overlay.d_qname.getStorage();
  *qname = storage.data();
  *qnameSize = storage.size();
}

uint16_t dnsdist_ffi_dnspacket_get_qtype(const dnsdist_ffi_dnspacket_t* packet)
{
  if (packet != nullptr) {
    return packet->overlay.d_qtype;
  }
  return 0;
}

uint16_t dnsdist_ffi_dnspacket_get_qclass(const dnsdist_ffi_dnspacket_t* packet)
{
  if (packet != nullptr) {
    return packet->overlay.d_qclass;
  }
  return 0;
}

uint16_t dnsdist_ffi_dnspacket_get_records_count_in_section(const dnsdist_ffi_dnspacket_t* packet, uint8_t section)
{
  if (packet == nullptr || section > DNSResourceRecord::ADDITIONAL) {
    return 0;
  }

  uint16_t count = 0;
  for (const auto& record : packet->overlay.d_records) {
    if (record.d_place == section) {
      count++;
    }
  }

  return count;
}

void dnsdist_ffi_dnspacket_get_record_name_raw(const dnsdist_ffi_dnspacket_t* packet, size_t idx, const char** name, size_t* nameSize)
{
  if (packet == nullptr || name == nullptr || nameSize == nullptr || idx >= packet->overlay.d_records.size()) {
    return;
  }
  const auto& storage = packet->overlay.d_records.at(idx).d_name.getStorage();
  *name = storage.data();
  *nameSize = storage.size();
}

uint16_t dnsdist_ffi_dnspacket_get_record_type(const dnsdist_ffi_dnspacket_t* packet, size_t idx)
{
  if (packet == nullptr || idx >= packet->overlay.d_records.size()) {
    return 0;
  }
  return packet->overlay.d_records.at(idx).d_type;
}

uint16_t dnsdist_ffi_dnspacket_get_record_class(const dnsdist_ffi_dnspacket_t* packet, size_t idx)
{
  if (packet == nullptr || idx >= packet->overlay.d_records.size()) {
    return 0;
  }
  return packet->overlay.d_records.at(idx).d_class;
}

uint32_t dnsdist_ffi_dnspacket_get_record_ttl(const dnsdist_ffi_dnspacket_t* packet, size_t idx)
{
  if (packet == nullptr || idx >= packet->overlay.d_records.size()) {
    return 0;
  }
  return packet->overlay.d_records.at(idx).d_ttl;
}

uint16_t dnsdist_ffi_dnspacket_get_record_content_length(const dnsdist_ffi_dnspacket_t* packet, size_t idx)
{
  if (packet == nullptr || idx >= packet->overlay.d_records.size()) {
    return 0;
  }
  return packet->overlay.d_records.at(idx).d_contentLength;
}

uint16_t dnsdist_ffi_dnspacket_get_record_content_offset(const dnsdist_ffi_dnspacket_t* packet, size_t idx)
{
  if (packet == nullptr || idx >= packet->overlay.d_records.size()) {
    return 0;
  }
  return packet->overlay.d_records.at(idx).d_contentOffset;
}

void dnsdist_ffi_dnspacket_free(dnsdist_ffi_dnspacket_t* packet)
{
  if (packet != nullptr) {
    delete packet;
  }
}

#ifndef KYTY_DRIVER_CACHE_POLICY_H_
#define KYTY_DRIVER_CACHE_POLICY_H_
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <fmt/format.h>
#include <xxhash.h>

namespace Libs::Graphics::DriverCachePolicy {
struct HostIdentity {
  uint32_t vendor=0,device=0,driver=0,optimization=0;
  std::array<uint8_t,16> uuid{};
};
inline std::string HostSuffix(const HostIdentity& host) {
  constexpr char hex[]="0123456789abcdef";
  std::string uuid(32,'0');
  for(size_t i=0;i<host.uuid.size();++i) {
    uuid[2*i]=hex[host.uuid[i]>>4u]; uuid[2*i+1]=hex[host.uuid[i]&15u];
  }
  return fmt::format("{:08x}:{:08x}:{:08x}:{}:opt={}\n",
      host.vendor,host.device,host.driver,uuid,host.optimization);
}
// Journal metadata is compiler-owned; keep the exact source/version contract.
inline std::string JournalSignature(const HostIdentity& host,std::string_view source) {
  return fmt::format("KytyPC3:{}:{}",source,HostSuffix(host));
}
// The driver matches actual SPIR-V/layout/create-info, independent of our source.
inline std::string BlobSignature(const HostIdentity& host,[[maybe_unused]] std::string_view source) {
  return "KytyPC4:"+HostSuffix(host);
}
inline std::string HostPrefix(const HostIdentity& host) {
  constexpr char hex[]="0123456789abcdef";
  std::string uuid(32,'0');
  for(size_t i=0;i<host.uuid.size();++i) {
    uuid[2*i]=hex[host.uuid[i]>>4u]; uuid[2*i+1]=hex[host.uuid[i]&15u];
  }
  return fmt::format("{:08x}:{:08x}:{:08x}:{}",
      host.vendor,host.device,host.driver,uuid);
}
inline size_t LegacyBlobHeaderBytes(const HostIdentity& host,std::string_view diagnostic) {
  return std::string_view("KytyPC3:").size()+64u+1u+HostSuffix(host).size()+diagnostic.size();
}
inline bool BlobHeaderCompatible(std::string_view stored,const HostIdentity& host,
    std::string_view source,std::string_view diagnostic) {
  if(stored==BlobSignature(host,source)+std::string(diagnostic))return true;
  if(!stored.starts_with("KytyPC3:") && !stored.starts_with("KytyPC4:"))return false;
  if(!diagnostic.empty() && !stored.ends_with(diagnostic))return false;
  return stored.find(HostPrefix(host))!=std::string_view::npos;
}
inline size_t MaxBlobHeaderBytes([[maybe_unused]] const HostIdentity& host,
    [[maybe_unused]] std::string_view source,
    [[maybe_unused]] std::string_view diagnostic) {
  return 512u;
}
inline size_t FindBlobHeaderBytes(std::span<const char> probe,const HostIdentity& host,
    std::string_view source,std::string_view diagnostic) {
  if(probe.size()<16u)return 0u;
  const std::string_view str(probe.data(),probe.size());
  if(!str.starts_with("KytyPC3:") && !str.starts_with("KytyPC4:"))return 0u;
  const auto nl=str.find('\n');
  if(nl==std::string_view::npos)return 0u;
  size_t header_len=nl+1u;
  if(!diagnostic.empty()) {
    if(str.substr(header_len).starts_with(diagnostic)) {
      header_len+=diagnostic.size();
    }
  }
  if(header_len<=probe.size() &&
     BlobHeaderCompatible(str.substr(0,header_len),host,source,diagnostic)) {
    return header_len;
  }
  return 0u;
}
inline bool EnvelopeSizeValid(uint64_t size,size_t header) {
  return header!=0u && header<=UINT32_MAX-sizeof(uint64_t) &&
      size>=header+sizeof(uint64_t) && size<=UINT32_MAX;
}
inline bool PayloadChecksumMatches(std::span<const uint8_t> payload,uint64_t expected) {
  return XXH3_64bits(payload.data(),payload.size())==expected;
}
inline bool PayloadHostHeaderMatches(std::span<const uint8_t> bytes,const HostIdentity& host) {
  if(bytes.size()<32u)return false;
  const auto u32=[&](size_t offset) {
    return uint32_t{bytes[offset]} | uint32_t{bytes[offset+1]}<<8u |
        uint32_t{bytes[offset+2]}<<16u | uint32_t{bytes[offset+3]}<<24u;
  };
  return u32(0)==32u && u32(4)==1u && u32(8)==host.vendor && u32(12)==host.device &&
      std::equal(host.uuid.begin(),host.uuid.end(),bytes.begin()+16u);
}
} // namespace Libs::Graphics::DriverCachePolicy
#endif

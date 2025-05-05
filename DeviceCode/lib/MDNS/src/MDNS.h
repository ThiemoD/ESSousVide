#include "application.h"

#ifndef _INCL_MDNS
#define _INCL_MDNS

#include "Buffer.h"
#include "Label.h"
#include "Record.h"
#include <map>
#include <vector>

#define MDNS_ADDRESS IPAddress(224, 0, 0, 251)
#define MDNS_PORT 5353

#define BUFFER_SIZE 512
#define HOSTNAME ""
#define META_SERVICE "_services._dns-sd._udp"

namespace mdns {
    
    class MDNS {
    public:
    
      bool setHostname(String hostname);
    
      bool addService(String protocol, String service, uint16_t port, String instance, std::vector<String> subServices = std::vector<String>());
    
      void addTXTEntry(String key, String value = "");
    
      bool begin(bool announce = false);
    
      bool processQueries();
    
    private:
    
      struct QueryHeader {
        uint16_t id;
        uint16_t flags;
        uint16_t qdcount;
        uint16_t ancount;
        uint16_t nscount;
        uint16_t arcount;
      };
    
      UDP * udp = new UDP();
      Buffer * buffer = new Buffer(BUFFER_SIZE);
    
      Label * ROOT = new Label("");
      Label * LOCAL = new Label("local", ROOT);
      MetaLabel * META = new MetaLabel("_services", new Label("_dns-sd", new Label("_udp", LOCAL)));
      Label::Matcher * matcher = new Label::Matcher();
    
      ARecord * aRecord;
      TXTRecord * txtRecord;
    
      std::map<String, Label *> labels;
      std::vector<Record *> records;
      String status = "Ok";
    
      QueryHeader readHeader(Buffer * buffer);
      void getResponses();
      void writeResponses();
      bool isAlphaDigitHyphen(String string);
      bool isNetUnicode(String string);
    };
    
}    
#endif

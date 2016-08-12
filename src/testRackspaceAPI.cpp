/// Tests chunked upload and zipped upload against the Rackspace cloud files API
/// https://developer.rackspace.com/docs/cloud-files/v1/developer-guide/#create-or-update-object


#include <RESTClient/base/logger.hpp>
#include <RESTClient/base/url.hpp>
#include <RESTClient/http/HTTP.hpp>
#include <RESTClient/http/Services.hpp>
#include <RESTClient/jobManagement/JobRunner.hpp>
#include <boost/iostreams/filtering_stream.hpp>

// From jsonpp11 external project in cmake
#include <json/io.hpp>
#include <json/value.hpp>

#include <iostream>
#include <sstream>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/coroutine2/all.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/regex.hpp>

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

// Generates A-Z 0-9 over and over
class AlphabetoSource {
public:
  using char_type = char;
  using category = boost::iostreams::source_tag;
private:
  using coro_t = boost::coroutines2::coroutine<char>;
  static void _generator(coro_t::push_type &sink) {
    while (true) {
      for (char c = 'A'; c != 'Z' + 1; ++c)
        sink(c);
      sink(' ');
      for (char c = '0'; c != '9' + 1; ++c)
        sink(c);
      sink(' ');
    }
  }
  coro_t::pull_type generator;
  std::streamsize limit;
  std::streamsize transmitted;
public:
  AlphabetoSource(size_t limit)
      : generator(AlphabetoSource::_generator), limit(limit), transmitted(0) {}
  std::streamsize read(char *s, std::streamsize n) {
    size_t toSend = std::min(transmitted, n);
    for (std::streamsize i = 0; i != toSend; ++i) {
      *s++ = generator.get();
      generator();
    }
    transmitted += toSend;
    return toSend;
  }
};

int main(int argc, char *argv[]) {

  using namespace std::placeholders;

  std::vector<RESTClient::QueuedJob> tests;

  // First we need to authenticate against the RS API, before we can get the URL for all the tests
  RESTClient::JobRunner jobs;

  namespace json = ciere::json;

  json::value info;
  RESTClient::Headers headers{{"Content-type", "application/json"}};

  RESTClient::URL syd_cf_url;
  std::string token;

  auto afterLogin = [&]() {
    // Now get the sydney cloud files URL
    token = info["access"]["token"]["id"];
    std::cout << "Token: " << token << std::endl;


    // Find the Sydney cloud files URL
    const json::value &catalog = info["access"]["serviceCatalog"];
    auto cf = std::find_if(
        catalog.begin_array(), catalog.end_array(),
        [](auto &service) { return service["name"] == "cloudFiles"; });
    assert(cf != catalog.end_array());
    auto& endpoints = (*cf)["endpoints"];
    auto ep = std::find_if(endpoints.begin_array(), endpoints.end_array(),
                           [](auto &ep) { return ep["region"] == "SYD"; });
    assert(ep != endpoints.end_array());
    syd_cf_url = (*ep)["publicURL"];

    std::cout << "Syd URL: " << syd_cf_url << std::endl;
    headers["X-Auth-Token"] = token;
    
    auto &q = jobs.queue(syd_cf_url.parts().hostname);

    q.emplace(RESTClient::QueuedJob{
        "Ensure container", syd_cf_url.parts().hostname,
        [&token](const std::string &name, const std::string &hostname,
                 RESTClient::HTTP &server) {
          // List containers
          auto response = server.get("/");
          std::istream &b = response.body;
          boost::iostreams::copy(b, std::cout);
          return true;
        }});

    jobs.startProcessing();

  };

  jobs.queue("https://identity.api.rackspacecloud.com").emplace(RESTClient::QueuedJob{
      "Login", "https://identity.api.rackspacecloud.com/v2.0/tokens",
      [&info, &headers, &afterLogin](const std::string &name, const std::string &hostname,
              RESTClient::HTTP &conn) {
        /*
        json::value j(json::value{{"auth", json::value{{"RAX-KSKEY:apiKeyCredentials",
                                   json::value{{"username", RS_USERNAME},
                                        {"apiKey", RS_APIKEY}}}}}});
        */
        using map = std::map<std::string, json::value>;
        map jdata{{"auth", map{{"RAX-KSKEY:apiKeyCredentials",
                                map{{"username", RS_USERNAME},
                                    {"apiKey", RS_APIKEY}}}}}};
        json::value j(jdata);

        RESTClient::HTTPRequest request{"POST", "/v2.0/tokens", headers};
        std::ostream &putter(request.body);
        putter << j;
        RESTClient::HTTPResponse response = conn.action(request);
        if (!((response.code >= 200) && (response.code < 300)))
          LOG_ERROR("Couldn't log in to RS: code("
                    << response.code << ") message (" << response.body << ")");
        std::string data(response.body);
        json::construct(data, info, true);
        afterLogin();
        return true;
      }});

  jobs.startProcessing();

  RESTClient::Services::instance().io_service.run();

  jobs.startProcessing();

  RESTClient::Services::instance().io_service.run();

  auto &q = jobs.queue(syd_cf_url.parts().hostname);

// Upload then download alphabeto to cloud files
/*
  q.emplace({"Chunked Transmit", syd_cf_url.parts().hostname, [&token](const std::string& name, const std::string& hostname, RESTClient::HTTP& server){
      // Upload
      boost::iostreams::filtering_stream<char> s;
      s.push(AlphabetoSource(1024 * 20));
      RESTClient::HTTPRequest r("POST", STRINGIFY(RS_CONTAINER_NAME) "/test1"); 
      // Download
  });
  */

  return 0;
}

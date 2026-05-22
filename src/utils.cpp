#ifdef USE_SSL
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include "../dep/cpp-httplib/httplib.h"

#include "utils.h"

namespace utils {

	std::string readWholeFile(const std::string& path) {
		// otevreni souboru
		std::ifstream ifs(path);
		if (!ifs) {
			return "";
		}

		// nacteni obsahu souboru
		std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
		return content;
	}

	std::string downloadHTML(const std::string& url) {

		std::string scheme;
		std::string rest;

		// extrahovani schematu a zbytku URL
		if (url.substr(0, 7) == "http://") {
			scheme = "http";
			rest = url.substr(7);
		}
		else if (url.substr(0, 8) == "https://") {
			scheme = "https";
			rest = url.substr(8);
		}
		else {
			return ""; // nezname schema
		}

		size_t pos = rest.find("/");
		std::string domain = rest.substr(0, pos);
		std::string path = rest.substr(pos);

		// stahne obsah stranky - pouzije SSL klienta, pokud je pozadovana podpora SSL
#ifdef USE_SSL
		httplib::SSLClient cli(domain.c_str());
		cli.enable_server_certificate_verification(false);
		cli.enable_server_hostname_verification(false);
#else
		httplib::Client cli(domain.c_str());
#endif

		cli.set_follow_location(true);
		auto res = cli.Get(path.c_str());

		if (!res || res->status != 200) {
			std::cerr << "Chyba: " << res->status << std::endl;
			return "";
		}

		return res->body;
	}

}

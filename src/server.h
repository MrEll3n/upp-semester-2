#pragma once

#include <string>
#include <memory>
#include <functional>

// dopredne deklarace
namespace httplib {
	class Server;
	struct Request;
	struct Response;
}

// server obstaravajici prijimani pozadavku
class CServer {
	public:
		// vychozi adresa pro naslouchani
		static constexpr const char* DefaultAddress = "localhost";
		// vychozi port pro naslouchani
		static constexpr int DefaultPort = 8001;

		// placeholder pro vysledky
		static constexpr const char* VysledkyPlaceholder = "<!-- VYSLEDKY -->";

	private:
		// HTML kod stranky, kterou bude server poskytovat
		std::string m_servedPage;

		// HTML kod stranky s vysledky
		std::string m_resultsPage;

		// adresa, na ktere bude server naslouchat
		std::string m_listenAddress{ DefaultAddress };
		// port, na kterem bude server naslouchat
		int m_listenPort{ DefaultPort };

		// instance serveru
		std::unique_ptr<httplib::Server> m_server;

		// callback pro zpracovani odeslanych URL
		std::function<void(const std::vector<std::string>&, std::string&)> m_onURLsReceived;

	protected:
		// obsluha GET pozadavku na hlavni stranku
		void Handle_Get_Any(const httplib::Request& req, httplib::Response& res);
		// obsluha POST pozadavku z formulare
		void Handle_Post_Form(const httplib::Request& req, httplib::Response& res);

	public:
		// konstruktor
		explicit CServer();

		~CServer();

		// inicializace serveru
		// basePath - cesta k adresari, kde se nachazi soubor "index.html"
		// listenAddress - adresa, na ktere bude server naslouchat
		// listenPort - port, na kterem bude server naslouchat
		// vraci true, pokud se inicializace povedla, jinak false
		bool Init(const std::string& basePath, const std::string& listenAddress = DefaultAddress, const int listenPort = DefaultPort);

		// registrace callbacku pro zpracovani odeslanych URL
		// onURLsReceived - callback pro zpracovani odeslanych URL
		void RegisterFormCallback(const std::function<void(const std::vector<std::string>&, std::string&)>& onURLsReceived) {
			m_onURLsReceived = onURLsReceived;
		}

		// spusteni serveru
		// vraci true, pokud se server podarilo spustit, jinak false
		bool Run();
};

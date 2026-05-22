#ifdef USE_SSL
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include "../dep/cpp-httplib/httplib.h"

#include <cassert>
#include <iostream>
#include <filesystem>

#include "server.h"
#include "utils.h"

CServer::CServer() : m_server{ std::make_unique<httplib::Server>() } {
	// kontrola, zda byla instance vytvorena
	assert(m_server && "Nelze vytvorit instanci httplib::Server");
}

CServer::~CServer() {
	// zastavi server
	if (m_server && m_server->is_running()) {
		m_server->stop();
	}
}

bool CServer::Init(const std::string& basePath, const std::string& listenAddress, const int listenPort) {

	m_listenAddress = listenAddress;
	m_listenPort = listenPort;

	// zkontroluje, zda existuje cesta basePath
	auto path = std::filesystem::path{ basePath };
	if (!std::filesystem::exists(path)) {
		std::cerr << "Uvedena cesta neexistuje: " << basePath << std::endl;
		return false;
	}

	// zkontroluje, zda existuje soubor index.html
	path /= "index.html";
	if (!std::filesystem::exists(path)) {
		std::cerr << "Soubor neexistuje: " << path << std::endl;
		return false;
	}

	// nacte obsah souboru index.html
	m_servedPage = utils::readWholeFile(path.string());

	// kontrola, zda se obsah souboru nacetl
	if (m_servedPage.empty()) {
		std::cerr << "Nelze precist soubor: " << path << std::endl;
		return false;
	}

	// zkontroluje, zda existuje soubor results.html
	path.replace_filename("results.html");
	if (!std::filesystem::exists(path)) {
		std::cerr << "Soubor neexistuje: " << path << std::endl;
		return false;
	}

	// nacte obsah souboru results.html
	m_resultsPage = utils::readWholeFile(path.string());

	// kontrola, zda se obsah souboru nacetl
	if (m_resultsPage.empty()) {
		std::cerr << "Nelze precist soubor: " << path << std::endl;
		return false;
	}

	// registrace obsluhy pozadavku

	m_server->Get("/", std::bind(&CServer::Handle_Get_Any, this, std::placeholders::_1, std::placeholders::_2));
	m_server->Post("/submit", static_cast<httplib::Server::Handler>(std::bind(&CServer::Handle_Post_Form, this, std::placeholders::_1, std::placeholders::_2)));

	m_server->set_error_handler([](const httplib::Request& req, httplib::Response& res) {
		// pokud je status kod 500, uz jsme osetrili chybu jako vyjimku v handleru nize
		if (res.status != 500) {
			// pro jednoduchost povazujme vsechny ostatni chyby za 404
			res.status = 404;
			res.set_content("Stranka nebyla nalezena", "text/plain");
		}
	});

	m_server->set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr e) {
		// nastavit status kod 500 - interni chyba serveru
		res.status = 500;

		try {
			if (e) {
				std::rethrow_exception(e);
			}
		}
		catch (const std::exception& ex) {
			res.set_content("Doslo k vyjimce pri zpracovani pozadavku: " + std::string(ex.what()), "text/plain");
		}
		catch (...) {
			res.set_content("Neznama vyjimka", "text/plain");
		}
	});

	return true;
}

bool CServer::Run() {

	bool success = m_server && m_server->bind_to_port(m_listenAddress.c_str(), m_listenPort);

	if (success) {
		std::cout << "Server posloucha na http://" << m_listenAddress << ":" << m_listenPort << "/" << std::endl;
		std::cout << "Tuto stranku zobrazte v prohlizeci a zadejte URL adresy, ktere chcete zpracovat." << std::endl;
	}
	else {
		std::cerr << "Nelze spustit server!" << std::endl;
	}

	return success && m_server->listen_after_bind();
}

void CServer::Handle_Get_Any(const httplib::Request& req, httplib::Response& res) {
	// jen odesle nacteny obsah souboru index.html
	res.set_content(m_servedPage, "text/html");
}

void CServer::Handle_Post_Form(const httplib::Request& req, httplib::Response& res) {
	// zpracuje odeslany formular - kontrola parametru
	if (!req.has_param("vstup")) {
		res.status = 400;
		res.set_content("Chybejici parametr 'vstup'", "text/plain");
		return;
	}

	const std::string vstup = req.get_param_value("vstup");

	if (vstup.empty()) {
		res.status = 400;
		res.set_content("Prazdny parametr 'vstup'", "text/plain");
		return;
	}

	std::vector<std::string> lines;
	std::istringstream iss{ vstup };
	std::string line;
	while (std::getline(iss, line)) {
		lines.push_back(line);
	}

	std::string vystup;

	if (m_onURLsReceived) {
		m_onURLsReceived(lines, vystup);
	}

	const std::string Placeholder{ VysledkyPlaceholder };

	// nahradit vysledky do stranky results.html
	std::string resultsPage = m_resultsPage;
	size_t pos = resultsPage.find(Placeholder);
	if (pos != std::string::npos) {
		resultsPage.replace(pos, Placeholder.size(), vystup);
	}

	res.set_content(resultsPage, "text/html");
}

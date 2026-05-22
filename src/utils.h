#pragma once

#include <string>
#include <sstream>
#include <fstream>

namespace utils {
	// precte cely soubor do retezce
	// path - cesta k souboru
	// vraci obsah souboru nebo prazdny retezec v pripade chyby
	std::string readWholeFile(const std::string& path);

	// stahne HTML kod stranky z dane URL
	// url - adresa stranky
	// vraci obsah stranky nebo prazdny retezec v pripade chyby
	std::string downloadHTML(const std::string& url);
}

#include "PKG.h"
#include "common.h"
#include "fsFile.h"
#include "sha1.h"

PKG::PKG()
{
}


PKG::~PKG()
{
}

bool PKG::open(const std::string& filepath) {
	fsFile file;
	if (!file.Open(filepath, fsRead))
	{
		return false;
	}

	const U64 pkgSize = file.getFileSize();
	pkg.resize(pkgSize);
	file.Seek(0, fsSeekSet);
	file.Read(&pkg[0], pkgSize);

	const auto& header = (PKGHeader&)pkg[0];
	
	const auto& footer = (PKGFooter&)pkg[pkgSize-0x20];

	unsigned char resultsha1[20];
	sha1(&pkg[0], pkgSize-0x20, resultsha1);
	if (memcmp(footer.sha1, resultsha1, 20) != 0)
	{
		return false;
	}
	return true;
}
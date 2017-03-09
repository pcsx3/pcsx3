#pragma once
#include <vector>
#include "types.h"

enum PKGRevision : U16 {
	PKG_REVISION_DEBUG = 0x0000,
	PKG_REVISION_RELEASE = 0x8000,
};

enum PKGType : U16 {
	PKG_TYPE_PS3 = 0x0001,  // PlayStation 3
	PKG_TYPE_PSP = 0x0002,  // PlayStation Portable
};

struct PKGHeader {
	/*BE*/U32 magic;                  // Magic
	/*BE*/U16 pkg_revision;           // Revision
	/*BE*/U16 pkg_type;               // Type
	U32 pkg_info_offset;        // Info offset
	U32 pkg_info_count;         // Info count
	U32 header_size;            // Header size (usually 0xC0)
	U32 item_count;             // Files and folders in the encrypted data
	U64 total_size;             // Total PKG file size
	U64 data_offset;            // Encrypted data offset
	U64 data_size;              // Encrypted data size
	S08 contentid[48];               // Content ID (similar to "XX####-XXXX#####_##-XXXXXXXXXXXX####")
	S08 digest[16];                  // SHA1 hash from files and attributes
	S08 klicensee[16];               // AES-128-CTR IV
	S08 header_cmac_hash[16];        // CMAC OMAC hash of [0x00, 0x7F]
	S08 header_npdrm_signature[40];  // Header NPDRM ECDSA (R_sig, S_sig)
	S08 header_sha1_hash[8];         // Last 8 bytes of SHA1 of [0x00, 0x7F]
};

struct PKGEntry {
	U32 name_offset;
	U32 name_size;
	U64 data_offset;
	U64 data_size;
	U32 type;
	U32 padding;
};
struct PKGFooter
{
	S08 sha1[20];//size??
};

class PKG
{
private:
	std::vector<U08> pkg;
	U64 pkgSize = 0;
	S08 pkgSHA1[20];
public:
	PKG();
	~PKG();
	bool open(const std::string& filepath);
	U64  getPkgSize();
	S08*  getPkgSHA1();
	bool extract(const std::string& filepath,std::string& failreason);
};


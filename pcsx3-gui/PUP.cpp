#include "types.h"
#include "PUP.h"
#include "fsFile.h"
#include "aes.h"
#include "..\pcsx3\ext\zlib\zlib.h"
#include <direct.h>  
#include <stdlib.h>  
#include <stdio.h> 
#include "../pcsx3-gui/pcsx3gui.h"
#include "../pcsx3/src/crypto.keyvault.h"
//#include "bench.h"
using namespace std;

PUP::PUP()
{

}
PUP::~PUP()
{

}
bool PUP::Read(const std::string& filepath, const std::string& extractPath)
{
	fsFile file;
	file.Open(filepath, fsRead);
	PUPHeader pupheader;
	file.ReadBE(pupheader);
	if (pupheader.magic >> 32 != 0x53434555)//magic is not correct
	{
		file.Close();
		return false;
	}
	else
	{
		printPUPHeader(pupheader);
		
		U64 filenumber = pupheader.file_count;
		for (int i = 0; i < pupheader.file_count; i++)
		{
			file.Seek(0x30 + (0x20 * i), fsSeekSet);
			PUPFileEntry fentry;
			file.ReadBE(fentry);
			if (fentry.entry_id == 0x300)
			{
				printPUPFileEntries(pupheader, fentry);
				U64 length = fentry.data_length;
				U64 offset = fentry.data_offset;
	
				char *f = new char[length];
				file.Seek(offset, fsSeekSet);
				file.Read(f, length);
				int off = 0;
				for (;;)
				{
					tar_header& tarh = (tar_header&)f[off];
					int filesize = parseoct(tarh.size, 12);
					if (tarh.typeflag == '0')
					{
						offset_map[tarh.name] = off;
					}
					off += ((filesize+sizeof(tar_header) + 512 - 1) & ~(512 - 1));//alligned to 512 bytes
					if (off > length)
						break;
				}
				for (auto it = offset_map.cbegin(); it != offset_map.cend(); ++it)
				{
					if (it->first.find("dev_flash_") == 0)//find and extract dev_flash files for now
					{
						tar_header& tarh = (tar_header&)f[it->second];
						int filesize = parseoct(tarh.size, 12);
						int foffset = ((it->second + sizeof(tar_header) + 512 - 1) & ~(512 - 1));
						//store file in temp memory
						U08 *devf = new U08[filesize];
						memcpy(devf, f + foffset, filesize);

						//decrypt pkg
						U32 decryptedfilesize = 0;
						U08* decrypted =decryptpkg(devf, decryptedfilesize);
						int off = 0;
						for (;;)
						{
							tar_header& tarh = (tar_header&)decrypted[off];
							int filesize = parseoct(tarh.size, 12);
							if (tarh.typeflag == '5')
							{
								_mkdir(tarh.name);//create_dir((char*)decrypted, /*parseoct(decrypted + 100, 8)*/0);
							}
							if (tarh.typeflag == '0')
							{
								int foffset = ((off + sizeof(tar_header) + 512 - 1) & ~(512 - 1));
								fsFile tar;
								tar.Open(tarh.name, fsWrite);
								tar.Write(foffset+decrypted, filesize);
								tar.Close();
							}
							off += ((filesize + sizeof(tar_header) + 512 - 1) & ~(512 - 1));//alligned to 512 bytes
							if (off > decryptedfilesize)
								break;
						}

						delete[] devf;
						delete[] decrypted;
					}
				}
				delete[] f;
			}
			else //just print entry info
			{
				printPUPFileEntries(pupheader, fentry);
			}
		}
		file.Close();
		return true;
	}
}
/* Parse an octal number, ignoring leading and trailing nonsense. */
int PUP::parseoct(const char *p, size_t n)
{
	int i = 0;

	while ((*p < '0' || *p > '7') && n > 0) {
		++p;
		--n;
	}
	while (*p >= '0' && *p <= '7' && n > 0) {
		i *= 8;
		i += *p - '0';
		++p;
		--n;
	}
	return (i);
}
/*
    decypt pkg from pup . This is a SCE file so it would be generic for self's as well later
*/
U08* PUP::decryptpkg(U08 *pkg,U32 &filesize)
{
	U16 flags;
	U16 type;
	U32 hdr_len;
	U64 dec_size;
	struct keylist *k;
	const auto& sce_header = (SceHeader&)pkg[0x0];

	flags = FromBigEndian(sce_header.key_revision);
	type = FromBigEndian(sce_header.header_type);
	//hdr_len = FromBigEndian((U32&)pkg + 0x10);
	//dec_size = FromBigEndian((U64&)pkg + 0x18);
	sce_decrypt_header(pkg);
	filesize = FromBigEndian(sce_header.data_length) -0x80;
	U08* extracted = new U08[filesize];
	sce_decrypt_data(pkg,extracted);
	return extracted;
}
void PUP::sce_decrypt_header(U08 *ptr)
{
	U32 meta_offset;
	U32 meta_len;
	U64 header_len;
	U32 i, j;
	U08 tmp[0x40];
	int success = 0;

	const auto& sce_header = (SceHeader&)ptr[0x0];
	const auto& app_info = (AppInfo&)ptr[0x70];

	U16 htype = FromBigEndian(sce_header.header_type);
	const SelfKey* key = nullptr;
	if (htype == 3)//pkg found in PUP
	{
		//doesn't appear to have app_info
		key = getSelfKey(KEY_PKG, 0, 0);

	}
	else //probably we have app_info
	{
		//keyvault
		U32 self_type = FromBigEndian(app_info.self_type);
		U64 version = FromBigEndian(app_info.version);
		U16 key_revision = FromBigEndian(sce_header.key_revision);
		key = getSelfKey(self_type, version, key_revision);
	}
	


	meta_offset = FromBigEndian(sce_header.metadata_offset);
	header_len = FromBigEndian(sce_header.header_length);

	aes_context aes;
	// Decrypt Metadata Info
	U08 metadata_iv[0x10];
	memcpy(metadata_iv, key->riv, 0x10);
	aes_setkey_dec(&aes, key->erk, 256);
	auto& meta_info = (MetadataInfo&)ptr[sizeof(SceHeader) + FromBigEndian(sce_header.metadata_offset)];

	AES_CBC_decrypt(&aes,sizeof(MetadataInfo), metadata_iv, (U08*)&meta_info, (U08*)&meta_info);
	//aes_crypt_cbc(&aes, AES_DECRYPT, sizeof(MetadataInfo), metadata_iv, (U08*)&meta_info, (U08*)&meta_info);

	// Decrypt Metadata Headers (Metadata Header + Metadata Section Headers)
	U08 ctr_stream_block[0x10];
	U08* meta_headers = (U08*)&ptr[sizeof(SceHeader) + FromBigEndian(sce_header.metadata_offset) + sizeof(MetadataInfo)];
	U32 meta_headers_size = FromBigEndian(sce_header.header_length) - (sizeof(SceHeader) + FromBigEndian(sce_header.metadata_offset) + sizeof(MetadataInfo));
	U64 ctr_nc_off = 0;
	aes_setkey_enc(&aes, meta_info.key, 128);


	AES_CTR_encrypt(&aes, meta_headers_size, meta_info.iv, meta_headers, meta_headers);
	//aes_crypt_ctr(&aes, meta_headers_size, &ctr_nc_off, meta_info.iv, ctr_stream_block, meta_headers, meta_headers);

}
void PUP::sce_decrypt_data(U08 *ptr,U08 *extracted)
{
	const auto& sce_header = (SceHeader&)ptr[0x0];
	const U32 meta_header_off = sizeof(SceHeader) + FromBigEndian(sce_header.metadata_offset) + sizeof(MetadataInfo);
	const auto& meta_header = (MetadataHeader&)ptr[meta_header_off];
	const U08* data_keys = (U08*)&ptr[meta_header_off + sizeof(MetadataHeader) + FromBigEndian(meta_header.section_count) * sizeof(MetadataSectionHeader)];
	aes_context aes;
	
	U08* data = new U08[FromBigEndian(sce_header.data_length)];
	for (U32 i = 0; i < FromBigEndian(meta_header.section_count); i++) {
		const auto& meta_shdr = (MetadataSectionHeader&)ptr[meta_header_off + sizeof(MetadataHeader) + i * sizeof(MetadataSectionHeader)];
		U08* data_decrypted = new U08[FromBigEndian(meta_shdr.data_size)+15];//keep it 16byte aligned

		U08 data_key[0x10];
		U08 data_iv[0x10];
		if (FromBigEndian(meta_shdr.key_idx) == 0xffffffff || FromBigEndian(meta_shdr.iv_idx) == 0xffffffff)// first shdr appears to have some kind of info that doesn't seem to be encrpyted (in PUP files)
			continue;
		if (FromBigEndian(meta_shdr.encrypted) == 3)
		{
			memcpy(data_key, data_keys + FromBigEndian(meta_shdr.key_idx) * 0x10, 0x10);
			memcpy(data_iv, data_keys + FromBigEndian(meta_shdr.iv_idx) * 0x10, 0x10);
			memcpy(data_decrypted, &ptr[FromBigEndian(meta_shdr.data_offset)], FromBigEndian(meta_shdr.data_size));

			// Perform AES-CTR encryption on the data
			U08 ctr_stream_block[0x10] = {};
			U64 ctr_nc_off = 0;
			aes_setkey_enc(&aes, data_key, 128);
			/*MEASURE({*/ AES_CTR_encrypt(&aes, FromBigEndian(meta_shdr.data_size),data_iv,data_decrypted, data_decrypted); /*});*/
			//MEASURE({ aes_crypt_ctr(&aes, FromBigEndian(meta_shdr.data_size), &ctr_nc_off, data_iv, ctr_stream_block, data_decrypted, data_decrypted); });
			//infof(PUP, "File size %d Total clks : %.2f",FromBigEndian(meta_shdr.data_size), RDTSC_total_clk);
		}
		if (FromBigEndian(meta_shdr.compressed) == 2)
		{
			unsigned long length = FromBigEndian(sce_header.data_length) - 0x80;
			uncompress(data, &length, data_decrypted, (U32)FromBigEndian(meta_shdr.data_size));
			memcpy(extracted, data, length);
		}
		
		delete[] data_decrypted;
		delete[] data;
	}

}

//debug info
void PUP::printPUPHeader(PUPHeader puph)
{
	infof(PUP, "%-30s 0x%x","magic : ",(puph.magic)>>32);
	infof(PUP, "%-30s 0x%x","package_version : ",(puph.package_version));
	infof(PUP, "%-30s 0x%x","image_version : ", (puph.image_version));
	infof(PUP, "%-30s 0x%x","file_count : ", (puph.file_count));
	infof(PUP, "%-30s 0x%x","header_length : ", (puph.header_length));
	infof(PUP, "%-30s 0x%x","data_length : ", (puph.data_length));
}
void PUP::printPUPFileEntries(PUPHeader puph, PUPFileEntry fentry)
{
	infof(PUP, "%-30s %s", "entry_name : ", id2name((fentry.entry_id), entries,NULL));
	infof(PUP, "%-30s 0x%x", "entry_id : ", (fentry.entry_id));
	infof(PUP, "%-30s 0x%x", "data_offset : ", (fentry.data_offset));
	infof(PUP, "%-30s 0x%x", "data_length : ", (fentry.data_length));
}
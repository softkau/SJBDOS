#include "fat.hpp"

#include <cctype>
#include <cstring>
#include <memory>
#include <algorithm>

namespace fat {
	BPB* boot_volume_image;
	unsigned long bytes_per_cluster;
	unsigned long DirectoryEntry::per_cluster;

	namespace {
		unsigned long clus2_begin_sector;
	}

	void Initialize(void* volume_image) {
		boot_volume_image = reinterpret_cast<fat::BPB*>(volume_image);
		bytes_per_cluster = static_cast<unsigned long>(boot_volume_image->bpb_BytesPerSec) * boot_volume_image->bpb_SecPerClus;
		DirectoryEntry::per_cluster = bytes_per_cluster / sizeof(DirectoryEntry);

		clus2_begin_sector
			= static_cast<unsigned long>(boot_volume_image->bpb_RsvdSecCnt)
			+ static_cast<unsigned long>(boot_volume_image->bpb_NumFATs) * boot_volume_image->bpb_FATSz32;	
	}

	uintptr_t GetClusterAddr(unsigned long cluster_num) {
		auto sector_num = clus2_begin_sector + (cluster_num - 2) * boot_volume_image->bpb_SecPerClus;
		
		uintptr_t offset = sector_num * boot_volume_image->bpb_BytesPerSec;
		return reinterpret_cast<uintptr_t>(boot_volume_image) + offset;
	}

	void ReadName(const DirectoryEntry* entry, char* basename9, char* ext3) {
		basename9[8] = 0;
		ext3[3] = 0;
		memcpy(basename9, entry->dir_Name, 8);
		memcpy(ext3, entry->dir_Name + 8, 3);
		for (int i = 7; i >= 0 && basename9[i] == ' '; i--)
			basename9[i] = '\0';
		for (int i = 2; i >= 0 && ext3[i] == ' '; i--)
			ext3[i] = '\0';
	}

	DirectoryEntryPointer GetLongFileName(const DirectoryEntryPointer& entry_ptr, char* lfn) {
		lfn[0] = 0;

		// copies every even bytes(0th, 2th, ...) from src to dst (total n bytes)
		auto copy_every_even_bytes = [](void* dst, const void* src, size_t n) {
			for (size_t i = 0; i < n * 2; i++) {
				*((char*)dst + i) = *((const char*)src + i*2);
			}
		};

		DirectoryEntryPointer it = entry_ptr;

		while (!it.IsEndOfClusterchain()) {
			auto entry = it.getLFN();

			if (entry->dir_Attr != ATTR_LONG_NAME) {
				return nullptr;
			}

			auto idx = ((static_cast<uint32_t>(entry->dir_LFNOrder) & 0x3F) - 1) * 13;
			bool is_terminal = !!(entry->dir_LFNOrder & 0x40);
			copy_every_even_bytes(lfn + idx +  0, entry->dir_Name0, 5);
			copy_every_even_bytes(lfn + idx +  5, entry->dir_Name1, 6);
			copy_every_even_bytes(lfn + idx + 11, entry->dir_Name2, 2);

			++it;
			if (is_terminal) {
				return it;
			}
		}

		return nullptr;
	}

	uint32_t* GetFAT() {
		uintptr_t fat_offset = static_cast<unsigned long>(boot_volume_image->bpb_RsvdSecCnt) * boot_volume_image->bpb_BytesPerSec;
		uintptr_t fat = reinterpret_cast<uintptr_t>(boot_volume_image) + fat_offset;
		return reinterpret_cast<uint32_t*>(fat);
	}

	unsigned long NextCluster(unsigned long cluster) {
		uint32_t* fat = GetFAT();
		uint32_t next = fat[cluster];
		if (next >= 0x0ffffff8ul) {
			return kEndOfClusterchain;
		}
		return next;
	}

	bool CompareShortName(const DirectoryEntry* entry, const char* name) {
		unsigned char name83[11];
		memset(name83, 0x20, sizeof name83);

		int i = 0;
		int i83 = 0;
		for (; name[i] && i83 < sizeof name83; i++, i83++) {
			if (name[i] == '.') {
				i83 = 7;
				continue;
			}
			name83[i83] = std::toupper(name[i]);
		}

		return memcmp(entry->dir_Name, name83, sizeof name83) == 0;
	}

	bool IsSFN(const char* str) {
		return false;
	}

	void LFNtoSFN(char* dst, const char* lfn) {
		memset(dst, ' ', 8 + 3);

		auto map_to = [](char x) -> char {
			static constexpr char allowed_chars[] = " !#$%&'()-@^_`{}~";
			constexpr int n = sizeof allowed_chars;

			if (std::any_of(allowed_chars, allowed_chars + n, [x](char a) { return x == a; })
				|| (std::isalpha(x) && std::isupper(x))
				|| std::isdigit(x)) {
				return x;
			} else if (std::isalpha(x)) {
				return std::toupper(x);
			} else {
				return '_';
			}
		};

		const char* dot = strrchr(lfn, '.');
		if (dot) {
			for (int i = 0; i < 8 && i < dot - lfn; i++) {
				dst[i] = map_to(lfn[i]);
			}
			for (int i = 0; i < 3 && dot[1 + i]; i++) {
				dst[i] = map_to(dot[1 + i]);
			}
		} else {
			for (int i = 0; i < 8 && lfn[i]; i++) {
				dst[i] = map_to(lfn[i]);
			}
		}
	}

	void SFNtoLFN(char* dst, const char* sfn) {
		int basename_sz = 8;
		int extension_sz = 3;

		for (; basename_sz > 0 && isspace(sfn[basename_sz-1]); basename_sz--);
		for (; extension_sz > 0 && isspace(sfn[8 + extension_sz-1]); extension_sz--);
		
		memcpy(dst, sfn, basename_sz);
		dst[basename_sz] = '\0';
		if (extension_sz) {
			dst[basename_sz] = '.';
			memcpy(dst + basename_sz + 1, sfn + 8, extension_sz);
			dst[basename_sz + extension_sz + 1] = '\0';
		}
	}

	std::pair<const char*, bool> NextPathElement(const char* path, char* path_elem) {
		const char* next_slash = strchr(path, '/');
		if (next_slash == nullptr) {
			strcpy(path_elem, path);
			return { nullptr, false };
		}
		
		const auto elem_len = next_slash - path;
		strncpy(path_elem, path, elem_len);
		path_elem[elem_len] = '\0';
		return { &next_slash[1], true };
	}

	std::pair<DirectoryEntry*, bool> FindFile(const char* path, unsigned long directory_cluster) {
		if (path[0] == '/') { // absolute path
			directory_cluster = boot_volume_image->bpb_RootClus;
			++path;
		} else if (directory_cluster == 0) { // root
			directory_cluster = boot_volume_image->bpb_RootClus;
		}

		auto name_buf = std::make_unique<char>(256);
		auto path_elem = std::make_unique<char>(256);

		const auto [ next_path, post_slash ] = NextPathElement(path, path_elem.get());
		const bool path_last = next_path == nullptr || next_path[0] == '\0';		
		
		DirectoryEntryPointer entry_ptr = { directory_cluster, 0 };
		for (; !entry_ptr.IsEndOfClusterchain(); ++entry_ptr) {
			auto e = entry_ptr.get();
			if (e->dir_Name[0] == 0x00)
				goto not_found;
			
			if (e->dir_Attr == ATTR_LONG_NAME) {
				entry_ptr = GetLongFileName(entry_ptr, name_buf.get());
				if (strcmp(name_buf.get(), path_elem.get()) != 0)
					continue;
					
				e = entry_ptr.get();
			} else {
				SFNtoLFN(name_buf.get(), reinterpret_cast<const char*>(e->dir_Name));
				if (strcmp(name_buf.get(), path_elem.get()) != 0)
					continue;
			}
			
			if (e->dir_Attr == ATTR_DIRECTORY && !path_last) {
				return FindFile(next_path, GetFirstCluster(e));
			}
			else {
				return { e, post_slash };
			}
		}

	not_found:
		return { nullptr, post_slash };
	}

	size_t LoadFile(void* buf, size_t len, const DirectoryEntry* entry) {
		auto cluster = GetFirstCluster(entry);

		const auto buf_uint8 = reinterpret_cast<uint8_t*>(buf);
		const auto buf_end = buf_uint8 + len;
		auto p = buf_uint8;

		while (cluster && cluster != fat::kEndOfClusterchain) {
			if (bytes_per_cluster >= buf_end - p) {
				memcpy(p, GetSectorByCluster<uint8_t>(cluster), buf_end - p);
				return len;
			}
			memcpy(p, GetSectorByCluster<uint8_t>(cluster), bytes_per_cluster);
			p += bytes_per_cluster;
			cluster = NextCluster(cluster);
		}
		return p - buf_uint8;
	}

	unsigned long ExtendCluster(unsigned long eoc_cluster, size_t count) {
		uint32_t* fat = GetFAT();
		while (IsEndOfClusterchain(fat[eoc_cluster])) {
			eoc_cluster = fat[eoc_cluster];
		}

		size_t num_allocated = 0;
		auto cur_clus = eoc_cluster;

		for (unsigned long cand = 2; num_allocated < count; cand++) {
			if (fat[cand] != 0) {
				continue;
			}

			fat[cur_clus] = cand;
			cur_clus = cand;
			++num_allocated;
		}
		fat[cur_clus] = kEndOfClusterchain;
		return cur_clus;
	}

	// create new entry in dir_cluster
	DirectoryEntry* AllocateEntry(unsigned long dir_cluster) {
		DirectoryEntryPointer entry_ptr = { dir_cluster, 0 };
		while (true) {
			auto entry = entry_ptr.get();
			if (entry->dir_Name[0] == 0 || entry->dir_Name[0] == 0xe5) { // empty entry
				return entry;
			}

			auto next_ptr = ++entry_ptr;
			if (next_ptr.IsEndOfClusterchain()) break;
			entry_ptr = next_ptr;
		}

		dir_cluster = ExtendCluster(entry_ptr.cluster, 1);
		auto dir = GetSectorByCluster<DirectoryEntry>(dir_cluster);
		memset(dir, 0, bytes_per_cluster);
		return dir;
	}

	void SetShortFileName(DirectoryEntry& entry, const char* name) {
		LFNtoSFN(reinterpret_cast<char*>(entry.dir_Name), name);
	}

	FileDescriptor::FileDescriptor(DirectoryEntry& fat_entry) : fat_entry(fat_entry) {

	}
	
	size_t FileDescriptor::Read(void* buf, size_t len) {
		if (rd_cluster == 0) {
			rd_cluster = GetFirstCluster(&fat_entry);
		}
		auto buf8 = reinterpret_cast<uint8_t*>(buf);
		// sanity check
		len = std::min(len, fat_entry.dir_FileSize - rd_offset);

		size_t total = 0;
		while (total < len) {
			auto sec = GetSectorByCluster<uint8_t>(rd_cluster);
			size_t n = std::min(len - total, bytes_per_cluster - rd_cluster_offset);
			memcpy(buf8 + total, sec + rd_cluster_offset, n);
			total += n;
			rd_cluster_offset += n;
			if (rd_cluster_offset == bytes_per_cluster) {
				rd_cluster = NextCluster(rd_cluster);
				rd_cluster_offset = 0;
			}
		}
		rd_offset += total;
		return total;
	}

	unsigned long AllocateClusterchain(size_t num_clusters) {
		uint32_t* fat = GetFAT();
		unsigned long first_cluster = 2;
		while (true) {
			if (fat[first_cluster] == 0) {
				fat[first_cluster] = kEndOfClusterchain;
				break;
			}
			first_cluster++;
		}

		if (num_clusters > 1) {
			ExtendCluster(first_cluster, num_clusters - 1);
		}
		return first_cluster;
	}

	size_t FileDescriptor::Write(const void* buf, size_t len) {
		auto required_clusters = [](size_t bytes) {
			return (bytes + bytes_per_cluster - 1) / bytes_per_cluster;
		};

		// get valid cluster
		if (wr_cluster == 0) {
			if (GetFirstCluster(&fat_entry) != 0) { // existing file entry
				wr_cluster = GetFirstCluster(&fat_entry);
			} else { // new entry
				wr_cluster = AllocateClusterchain(required_clusters(len));
				fat_entry.dir_FstClusLO = wr_cluster & 0xffff;
				fat_entry.dir_FstClusHI = (wr_cluster >> 16) & 0xffff;
			}
		}

		const uint8_t* buf8 = reinterpret_cast<const uint8_t*>(buf);

		size_t total = 0;
		while (total < len) {
			if (wr_cluster_offset == bytes_per_cluster) { // end of cluster chunk (go to next cluster)
				const auto next_cluster = NextCluster(wr_cluster);
				if (next_cluster == kEndOfClusterchain) {
					wr_cluster = ExtendCluster(wr_cluster, required_clusters(len - total));
				} else {
					wr_cluster = next_cluster;
				}
				wr_cluster_offset = 0;
			}

			auto sec = GetSectorByCluster<uint8_t>(wr_cluster);
			size_t n = std::min(len, bytes_per_cluster - wr_cluster_offset);
			memcpy(sec + wr_cluster_offset, buf8, n);
			total += n;
			wr_cluster_offset += n;
		}
		
		wr_offset += total;
		fat_entry.dir_FileSize = wr_offset;

		return total;
	}

	size_t FileDescriptor::Size() const {
		return fat_entry.dir_FileSize;
	}

	size_t FileDescriptor::Load(void* buf, size_t len, size_t offset) {
		FileDescriptor fd{fat_entry};
		fd.rd_offset = offset;

		unsigned long cluster = GetFirstCluster(&fat_entry);
		while (offset >= bytes_per_cluster) {
			offset -= bytes_per_cluster;
			cluster = NextCluster(cluster);
		}

		fd.rd_cluster = cluster;
		fd.rd_cluster_offset = offset;
		return fd.Read(buf, len);
	}

	WithError<DirectoryEntry*> CreateFile(const char* path) {
		auto parent_dir_cluster = fat::boot_volume_image->bpb_RootClus;
		const char* filename = path;

		// separate file name and parent directory path
		if (const char* slash_pos = strrchr(path, '/')) {
			filename = slash_pos + 1;
			if (slash_pos[1] == '\0') {
				return { nullptr, MakeError(Error::kIsDirectory) };
			}
			char parent_dir_name[slash_pos - path + 1]; // vla
			strncpy(parent_dir_name, path, slash_pos - path);
			parent_dir_name[slash_pos - path] = '\0';

			if (parent_dir_name[0] != '\0') {
				auto [ parent_dir, post_slash2 ] = fat::FindFile(parent_dir_name);
				if (parent_dir == nullptr) {
					return { nullptr, MakeError(Error::kNoSuchEntry) };
				}
				parent_dir_cluster = GetFirstCluster(parent_dir);
			}
		}

		// not handling lfn here (for now)
		auto dir = fat::AllocateEntry(parent_dir_cluster);
		if (dir == nullptr) {
			return { nullptr, MakeError(Error::kNoEnoughMemory) };
		}
		fat::SetShortFileName(*dir, filename);
		dir->dir_FileSize = 0;
		return { dir, MakeError(Error::kSuccess) };
	}
}
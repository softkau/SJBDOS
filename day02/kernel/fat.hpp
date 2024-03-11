#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>
#include "error.hpp"
#include "file.hpp"

namespace fat {
	struct BPB {
		uint8_t  bs_jmpBoot[3]; // jump instruction from program
		uint8_t  bs_OEMName[8]; // arbitrary 8-byte string
		uint16_t bpb_BytesPerSec; // bytes per block(sector)
		uint8_t  bpb_SecPerClus; // blocks per cluster
		uint16_t bpb_RsvdSecCnt; // the number of reserved blocks from beginning of the volume
		uint8_t  bpb_NumFATs; // the number of FATs
		uint16_t bpb_RootEntCnt; // the number of Root Entries(zero-d in FAT32)
		uint16_t bpb_TotSec16; // the number of blocks in given volume(zero-d in FAT32)
		uint8_t  bpb_Media; // type of media
		uint16_t bpb_FATSz16; // blocks per FAT(zero-d in FAT32)
		uint16_t bpb_SecPerTrk; // blocks per track
		uint16_t bpb_NumHeads; // the number of headers
		uint32_t bpb_HiddSec; // the number of hidden blocks
		uint32_t bpb_TotSec32; // the number of blocks in given volume(used in FAT32)
		uint32_t bpb_FATSz32; // blocks per FAT(used in FAT32)
		uint16_t bpb_ExtFlags; // flags for handling duplicate FATs
		uint16_t bpb_FSVer; // file system version
		uint32_t bpb_RootClus; // first cluster of root directory
		uint16_t bpb_FSInfo; // first block number of FSINFO struct
		uint16_t bpb_BkBootSec; // block number where the copy of boot sector is located
		uint8_t  bpb_Reserved[12];
		uint8_t  bs_DrvNum; // driver number used by BIOS INT 0x13
		uint8_t  bs_Reserved1;
		uint8_t  bs_BootSig; // extended signature
		uint32_t bs_VolID; // serial number of volume
		uint8_t  bs_VolLab[11]; // label of volume
		uint8_t  bs_FilSysType[8]; // file system type
	} __attribute__((packed));

	enum DirectoryAttribute : uint8_t {
		ATTR_READ_ONLY = 1 << 0,
		ATTR_HIDDEN = 1 << 1,
		ATTR_SYSTEM = 1 << 2,
		ATTR_VOLUME_ID = 1 << 3,
		ATTR_DIRECTORY = 1 << 4,
		ATTR_ARCHIVE = 1 << 5,
		ATTR_LONG_NAME = 0b00001111,
	};

	struct DirectoryEntry {
		uint8_t dir_Name[11]; // short file name
		DirectoryAttribute dir_Attr; // file attributes
		uint8_t dir_NTRes; // reserved for WindowsNT
		uint8_t dir_CrtTimeTenth; // Creation time in tenths of a second. Range 0-199 inclusive.
		uint16_t dir_CrtTime; // file creation time
		uint16_t dir_CrtDate; // file creation date
		uint16_t dir_LstAccDate; // last accessed date
		uint16_t dir_FstClusHI; // higher 2 byte of first cluster number
		uint16_t dir_WrtTime; // last write time
		uint16_t dir_WrtDate; // last write date
		uint16_t dir_FstClusLO; // lower 2 byte of first cluster number
		uint32_t dir_FileSize; // file size in bytes

		static unsigned long per_cluster;
	} __attribute__((packed));

	struct LongFileNameEntry {
		uint8_t dir_LFNOrder; // The order of this entry in the sequence of long file name entries.
		uint16_t dir_Name0[5];
		DirectoryAttribute dir_Attr; // always 0x0F
		uint8_t dir_LFNType; // long entry type. zero for name entries
		uint8_t dir_Checksum; // Checksum generated of the short file name when the file was created.
		uint16_t dir_Name1[6];
		uint8_t dir_Reserved[2]; // always zero
		uint16_t dir_Name2[2];
	} __attribute((packed));

	static_assert(sizeof(DirectoryEntry) == sizeof(LongFileNameEntry));

	constexpr auto GetFirstCluster(const DirectoryEntry* file_entry) { return (static_cast<uint32_t>(file_entry->dir_FstClusHI) << 16) | file_entry->dir_FstClusLO; }
	constexpr unsigned long kEndOfClusterchain = 0x0fffffff;

	void Initialize(void* volume_image);
	uintptr_t GetClusterAddr(unsigned long cluster_num);
	template <class T>
	T* GetSectorByCluster(unsigned long cluster_num) {
		return reinterpret_cast<T*>(GetClusterAddr(cluster_num));
	}
	unsigned long NextCluster(unsigned long cluster);

	struct DirectoryEntryPointer {
		unsigned long cluster;
		unsigned long index;

		DirectoryEntryPointer() {}
		DirectoryEntryPointer(unsigned long cluster, unsigned long index) : cluster(cluster), index(index) {}
		DirectoryEntryPointer(std::nullptr_t x) : cluster(0), index(0) {}

		DirectoryEntryPointer& operator++() {
			++index;
			if (index == DirectoryEntry::per_cluster) {
				cluster = NextCluster(cluster);
				index = 0;
			}
			return *this;
		}

		DirectoryEntry* get() const {
			return GetSectorByCluster<DirectoryEntry>(cluster) + index;
		}

		LongFileNameEntry* getLFN() const {
			return GetSectorByCluster<LongFileNameEntry>(cluster) + index;
		}

		bool IsEndOfClusterchain() const {
			return cluster >= 0x0ffffff8ul;
		}
	};

	inline bool IsEndOfClusterchain(unsigned long cluster) {
		return cluster >= 0x0ffffff8ul;
	}

	class FileDescriptor : public ::FileDescriptor {
	public:
		explicit FileDescriptor(DirectoryEntry& fat_entry);
		size_t Read(void* buf, size_t len) override;
		size_t Write(const void* buf, size_t len) override;
		size_t Size() const override;
		size_t Load(void* buf, size_t len, size_t offset) override;
	private:
		DirectoryEntry& fat_entry;
		size_t rd_offset = 0;
		unsigned long rd_cluster = 0;
		size_t rd_cluster_offset = 0;
		size_t wr_offset = 0;
		unsigned long wr_cluster = 0;
		size_t wr_cluster_offset = 0;
	};

	void ReadName(const DirectoryEntry* entry, char* basename9, char* ext4);
	DirectoryEntryPointer GetLongFileName(const DirectoryEntryPointer& entry_ptr, char* lfn);
	std::pair<DirectoryEntry*, bool> FindFile(const char* path, unsigned long directory_cluster=0);
	size_t LoadFile(void* buf, size_t len, const DirectoryEntry* entry);
	WithError<DirectoryEntry*> CreateFile(const char* path);

	extern BPB* boot_volume_image;
	extern unsigned long bytes_per_cluster;
}
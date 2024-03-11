#pragma once

#include <cstddef>
#include <cstdint>

namespace acpi {

	struct RSDP {
		char signature[8];
		uint8_t checksum;
		char oem_id[6];
		uint8_t revision;
		uint32_t rsdt_address;
		uint32_t length;
		uint64_t xsdt_address;
		uint8_t extended_checksum;
		char reserved[3];
	} __attribute__((packed));

	struct DescriptionHeader {
		char signature[4];
		uint32_t length;
		uint8_t revision;
		uint8_t checksum;
		char oem_id[6];
		char oem_table_id[8];
		uint32_t oem_revision;
		uint32_t creator_id;
		uint32_t creator_revision;
	} __attribute__((packed));

	struct XSDT {
		DescriptionHeader header;

		const DescriptionHeader& GetEntry(size_t i) const;
		size_t NumOfEntries() const;

	} __attribute__((packed));

	struct FADT {
		DescriptionHeader header;				// 0 ~ 35

		char reserved1[76 - sizeof(header)];	// 36 ~ 75
		uint32_t pm_tmr_blk;					// 76 ~ 79
		char reserved2[112 - 80];				// 80 ~ 111
		uint32_t flags;							// 112 ~ 115
		char reserved3[276 - 116];				// 116 ~ 275
	} __attribute__((packed));

	const uint32_t kPMTimerFreq = 3579545u; // ACPI PM 타이머 주기 3.579545MHz
	extern const FADT* fadt;

	// rsdp로 부터 acpi::fadt 포인터를 초기화한다
	void Initialize(const RSDP& rsdp);

	// ms 밀리초만큼 대기
	void WaitMilliseconds(const FADT* fadt, unsigned long ms);
}

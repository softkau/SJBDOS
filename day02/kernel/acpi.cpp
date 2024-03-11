#include "acpi.hpp"
#include "logger.hpp"
#include "asmfunc.h"

#include <cstdlib>
#include <cstring>

namespace {	
	template <typename T>
	uint8_t SumOfBytes(const T* data, size_t size) {
		return SumOfBytes(reinterpret_cast<const uint8_t*>(data), size);
	}

	template <>
	uint8_t SumOfBytes<uint8_t>(const uint8_t* data, size_t size) {
		uint8_t sum = 0;
		for (size_t i = 0; i < size; i++)
			sum += data[i];
		return sum;
	}

	bool ValidateRSDP(const acpi::RSDP& rsdp) {
		if (strncmp(rsdp.signature, "RSD PTR ", 8) != 0) {
			Log(kError, "invalid RSDP signatrue: %.8s\n", rsdp.signature);
			return false;
		}

		if (rsdp.revision != 2) {
			Log(kError, "RSDP revision no. must be 2; (%d) received.\n", rsdp.revision);
			return false;
		}

		if (auto sum = SumOfBytes(&rsdp, 20); sum != 0) {
			Log(kError, "RSDP checksum(20 bytes) vaildation failed; calculated checksum(%d)\n", sum);
			return false;
		}
		if (auto sum = SumOfBytes(&rsdp, 36); sum != 0) {
			Log(kError, "RSDP checksum(36 bytes) vaildation failed; calculated checksum(%d)\n", sum);
			return false;
		}

		return true;
	}

	bool VaildateHeader(const acpi::DescriptionHeader& header) {
		if (auto sum = SumOfBytes(&header, header.length) != 0) {
			Log(kError, "[acpi] header checksum vaildation failed; calculated checksum(%d)\n", sum);
			return false;
		}
		return true;
	}

	bool CompareSignature(const acpi::DescriptionHeader& header, const char* expected_signature) {
		if (strncmp(header.signature, expected_signature, 4) != 0) {
			Log(kDebug, "[acpi] expected description header signature %s; %s received\n", expected_signature, header.signature);
			return false;
		}
		return true;
	}
}

namespace acpi {
	const FADT* fadt;

	void Initialize(const RSDP& rsdp) {
		if (!ValidateRSDP(rsdp)) {
			Log(kError, "RSDP is not valid!\n");
			exit(1);
		}

		const auto& xsdt = *reinterpret_cast<const XSDT*>(rsdp.xsdt_address);
		if (!VaildateHeader(xsdt.header) || !CompareSignature(xsdt.header, "XSDT")) {
			Log(kError, "XSDT is not vaild!\n");
			exit(1);
		}

		fadt = nullptr;
		for (int i = 0; i < xsdt.NumOfEntries(); i++) {
			const auto& entry = xsdt.GetEntry(i);
			if (CompareSignature(entry, "FACP") && VaildateHeader(entry)) { // FADT has different signature name for historical reasons...
				fadt = reinterpret_cast<const FADT*>(&entry);
				break;
			}
		}

		if (fadt == nullptr) {
			Log(kError, "FADT is not found\n");
			exit(1);
		}
	}

	const DescriptionHeader& XSDT::GetEntry(size_t i) const {
		auto entries = reinterpret_cast<const uint64_t*>(&this->header + 1); // 첫 번째는 XSDT 헤더이므로...
		return *reinterpret_cast<const DescriptionHeader*>(entries[i]);
	}
	size_t XSDT::NumOfEntries() const {
		return (this->header.length - sizeof(DescriptionHeader)) / sizeof(uint64_t); // 첫 번째는 XSDT 헤더이므로...
	}

	void WaitMilliseconds(const FADT* fadt, unsigned long ms) {
		const bool pm_timer_32 = (fadt->flags >> 8) & 1; // 1인 경우, 32비트 타이머, 0인 경우 24비트 타이머
		const uint32_t start = IoIn32(fadt->pm_tmr_blk); // ACPI PM 타이머 값을 불러오기(kPMTimerFreq 주기마다 값이 0이 된다)
		uint32_t end = start + kPMTimerFreq * ms / 1000;
		if (!pm_timer_32) {
			end &= 0x00ffffffu;
		}

		if (end < start) // end가 overflow해서 start보다 값이 작아졌을 때
			while (IoIn32(fadt->pm_tmr_blk) >= start);
		
		while (IoIn32(fadt->pm_tmr_blk) < end); // 지정된 시간이 지날 때까지 기다리기
	}
}
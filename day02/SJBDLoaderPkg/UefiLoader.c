#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/BlockIo.h>
#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>
#include "elf.h"
#include "frame_buffer_config.h"
#include "memmap.h"

#define PAGE_SIZE 4096
#define REQUIRED_PAGES(BYTES) (((BYTES) + 0xFFF) / 0x1000)
typedef void EntryPoint_T(const struct FrameBufferConfig*, const struct MemoryMap*, const VOID*, VOID*);

/* Halts the process. The function never returns once it's called. */
void Halt(void) {
	while (1) __asm__("hlt");
}

/*
gets memory map and store it into map->buffer
@param map [buffer_size] and [buffer] must be configured beforehand. Other fields will be overwritten
@return EFI_SUCCESS when succeeded or else otherwise
*/
EFI_STATUS GetMemoryMap(struct MemoryMap* map);

/*
opens EFI file protocol of root directory
@param image_handle image handle object
@param root output pointer; EFI file protocol of root directory
@return EFI_SUCCESS when succeeded
*/
EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root);

/*
saves memory map(readable) into disk
@param map memory map struct to save
@param file EFI file to save memory map
@return EFI_SUCCESS when succeeded
*/
EFI_STATUS SaveMemoryMap(const struct MemoryMap* map, EFI_FILE_PROTOCOL* file);

/*
returns wstring that matches [type]
@param type memory field type(most likely an integer)
@return a wide string that corresponds to [type]
*/
const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type);

/*
tries to exit boot services and [gBS] will be unusable after the call
@param image_handle image handle object
@param map memory map
@returns EFI_SUCCESS when success
*/
EFI_STATUS ExitBootServices(EFI_HANDLE image_handle, struct MemoryMap* map);

/*
tries to read binary content of an opened [file] to memory.
When success, [*dst] will point to its content.
Memory for [dst] is allocated by this function.
@param file opened file handle to read
@param dst output pointer which points to content read from [file]. NULL when failed, and allocated memory is freed(if deallocation didn't fail).
@returns EFI_SUCCESS when success
*/
EFI_STATUS ReadFile(EFI_FILE_PROTOCOL* file, VOID** dst, const CHAR16* fname);
EFI_STATUS ReadBlocks(EFI_BLOCK_IO_PROTOCOL* block_io, UINT32 media_id, UINTN bytes, VOID** dst);

/*
loads [kernel_file] at base address specified by its ELF header.
[kernel_base_address] is an "owning pointer" and it's up to the caller to free its memory(unlikely, though)
@param kernel_file kernel(ELF file) to load
@param kernel_base_address set to where kernel has been loaded ([+24 bytes] is where the entry point pointer is stored)
@param kernel_memory_size set to how many bytes has been allocated for kernel
@return EFI_SUCCESS when success
*/
EFI_STATUS LoadKernel(EFI_FILE_PROTOCOL* kernel_file, EFI_PHYSICAL_ADDRESS* kernel_base_address, UINT64* kernel_memory_size);

/*
calculates the required memory address range specified by [ehdr](=ELF header).
This function does not fail.
@param ehdr pointer to ELF header
@param first set to the start of the range where LOAD-segments will be loaded into
@param last set to the end of the range where LOAD-segments will be loaded into
*/
void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last);

/*
copys LOAD-segments into memory specified by [ehdr](=ELF header).
It also adds 0-paddings if memsz > filesz for each LOAD-segment.
This function does not fail.
@param ehdr pointer to ELF header
*/
void CopyLoadSegments(Elf64_Ehdr* ehdr);

EFI_STATUS OpenGOP(EFI_HANDLE image_handle, EFI_GRAPHICS_OUTPUT_PROTOCOL** gop) {
	EFI_STATUS status;
	UINTN num_gop_handles = 0;
	EFI_HANDLE* gop_handles = NULL;

	status = gBS->LocateHandleBuffer(
		ByProtocol,
		&gEfiGraphicsOutputProtocolGuid,
		NULL,
		&num_gop_handles,
		&gop_handles
	);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = gBS->OpenProtocol(
		gop_handles[0],
		&gEfiGraphicsOutputProtocolGuid,
		(VOID**)gop,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
	);
	if (EFI_ERROR(status)) {
		return status;
	}

	FreePool(gop_handles);
	return EFI_SUCCESS;
}

EFI_STATUS OpenBlockIoProtocolForLoadedImage(EFI_HANDLE image_handle, EFI_BLOCK_IO_PROTOCOL** block_io) {
	EFI_STATUS status;
	
	EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
	status = gBS->OpenProtocol(
		image_handle,
		&gEfiLoadedImageProtocolGuid,
		(VOID**)&loaded_image,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
	);

	status = gBS->OpenProtocol(
		loaded_image->DeviceHandle,
		&gEfiBlockIoProtocolGuid,
		(VOID**)block_io,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
	);
	if (EFI_ERROR(status)) return status;

	return status;
}

// BOOT LOADER ENTRY POINT
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
	EFI_STATUS status;

	Print(L"[INFO] Hello, SJBD World!\n");
	
	CHAR8 memorymap_buffer[PAGE_SIZE * 4];
	struct MemoryMap memorymap = {
		.buffer_size = sizeof(memorymap_buffer),
		.buffer = memorymap_buffer,
		.map_size = 0,
		.map_key = 0,
		.descriptor_size = 0,
		.descriptor_version = 0
	};
	status = GetMemoryMap(&memorymap);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to obtain memory map: %r\n", status);
		Halt();
	}	

	EFI_FILE_PROTOCOL* root_directory;
	status = OpenRootDir(image_handle, &root_directory);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to open ROOT: %r\n", status);
		Halt();
	}

	EFI_FILE_PROTOCOL* memmap_file;
	status = root_directory->Open(root_directory, &memmap_file, L"\\memmap", EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to open/create /memmap: %r\n", status);
		Halt();
	}

	status = SaveMemoryMap(&memorymap, memmap_file);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to save memory map: %r\n", status);
		Halt();
	}
	status = memmap_file->Close(memmap_file);

	EFI_FILE_PROTOCOL* kernel_file;
	status = root_directory->Open(root_directory, &kernel_file, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to open kernel.elf: %r\n", status);
		Halt();
	}

	EFI_PHYSICAL_ADDRESS kernel_base_address;
	UINT64 kernel_memory_size;
	status = LoadKernel(kernel_file, &kernel_base_address, &kernel_memory_size);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to load kernel: %r\n", status);
		Halt();
	}

	// FAT DISK 볼륨 이미지 읽기
	VOID* volume_image;
	EFI_FILE_PROTOCOL* volume_file;
	status = root_directory->Open(root_directory, &volume_file, L"\\fat_disk", EFI_FILE_MODE_READ, 0);
	if (status == EFI_SUCCESS) {
		status = ReadFile(volume_file, &volume_image, L"fat_disk");
		if (EFI_ERROR(status)) {
			Print(L"failed to read volume file: %r\n", status);
			Halt();
		}
	}
	else { // FAT DISK가 없는 경우, 부팅 미디어 전체를 읽기
		EFI_BLOCK_IO_PROTOCOL* block_io;
		status = OpenBlockIoProtocolForLoadedImage(image_handle, &block_io);
		if (EFI_ERROR(status)) {
			Print(L"failed to open Block I/O Protocol: %r\n", status);
			Halt();
		}

		EFI_BLOCK_IO_MEDIA* media = block_io->Media;
		UINTN volume_bytes = (UINTN)media->BlockSize * (media->LastBlock + 1);
		if (volume_bytes > 32 * 1024 * 1024)
			volume_bytes = 32 * 1024 * 1024;

		Print(L"Reading %lu bytes (Present %d, BlockSize %u, LastBlock %u)\n", volume_bytes, media->MediaPresent, media->BlockSize, media->LastBlock);
		status = ReadBlocks(block_io, media->MediaId, volume_bytes, &volume_image);
		if (EFI_ERROR(status)) {
			Print(L"failed to read blocks: %r\n", status);
			Halt();
		}
	}

	EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
	status = OpenGOP(image_handle, &gop);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to open EFI_GRAPHICS_OUTPUT_PROTOCOL: %r\n", status);
		Halt();
	}
	struct FrameBufferConfig fbconf = {
		(UINT8*)gop->Mode->FrameBufferBase,
		gop->Mode->Info->PixelsPerScanLine,
		gop->Mode->Info->HorizontalResolution,
		gop->Mode->Info->VerticalResolution,
		0
	};
	switch (gop->Mode->Info->PixelFormat) {
		case PixelRedGreenBlueReserved8BitPerColor: fbconf.pixel_format = kPixelRGBResv8BitPerColor; break;
		case PixelBlueGreenRedReserved8BitPerColor: fbconf.pixel_format = kPixelBGRResv8BitPerColor; break;
		default: Print(L"[ERROR] Unsupported pixel format: %d\n", gop->Mode->Info->PixelFormat); Halt();
	}

	// Get RSDP (Root System Description Pointer)
	VOID* acpi_table = NULL;
	for (UINTN i = 0; i < system_table->NumberOfTableEntries; i++) {
		if (CompareGuid(&gEfiAcpiTableGuid, &system_table->ConfigurationTable[i].VendorGuid)) {
			acpi_table = system_table->ConfigurationTable[i].VendorTable;
			break;
		}
	}

	Print(L"[INFO]Kernel loaded at: 0x%0lx(%lu bytes)\n", kernel_base_address, kernel_memory_size);
	Print(L"[INFO]Exiting Boot Services and launching Kernel...\n");
	status = ExitBootServices(image_handle, &memorymap);
	if (EFI_ERROR(status)) {
		Halt();
	}

	// launch kernel!
	UINT64 entry_addrs = *(UINT64*)(kernel_base_address + 24);
	EntryPoint_T* entry_point = (EntryPoint_T*)entry_addrs;
	entry_point(&fbconf, &memorymap, acpi_table, volume_image);

	return EFI_SUCCESS;
}

EFI_STATUS ReadFile(EFI_FILE_PROTOCOL* file, VOID** dst, const CHAR16* fname) {
	EFI_STATUS status;
	UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 12;
	CHAR8 file_info_buffer[file_info_size];
	status = file->GetInfo(file, &gEfiFileInfoGuid, &file_info_size, file_info_buffer);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to open file info(%s): %r\n", fname, status);
		return status;
	}
	EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
	UINTN file_size = file_info->FileSize;

	status = gBS->AllocatePool(EfiLoaderData, file_size, dst);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to allocate pool: %r\n", status);
		return status;
	}
	status = file->Read(file, &file_size, *dst);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to Read File(%s): %r\n", fname, status);

		status = gBS->FreePool(*dst);
		if (EFI_ERROR(status)) {
			Print(L"[ERROR]Failed to free memory pool: %r\n", status);
			return status;
		}
		*dst = NULL;
		return status;
	}
	
	return status;
}

EFI_STATUS ReadBlocks(EFI_BLOCK_IO_PROTOCOL* block_io, UINT32 media_id, UINTN bytes, VOID** dst) {
	EFI_STATUS status;

	status = gBS->AllocatePool(EfiLoaderData, bytes, dst);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to allocate pool: %r\n", status);
		return status;
	}
	status = block_io->ReadBlocks(block_io, media_id, 0, bytes, *dst);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to Read Blocks: %r\n", status);

		status = gBS->FreePool(*dst);
		if (EFI_ERROR(status)) {
			Print(L"[ERROR]Failed to free memory pool: %r\n", status);
			return status;
		}
		*dst = NULL;
		return status;
	}
	return status;
}

EFI_STATUS LoadKernel(EFI_FILE_PROTOCOL* kernel_file, EFI_PHYSICAL_ADDRESS* kernel_base_address, UINT64* kernel_memory_size) {
	EFI_STATUS status;

	// 1. read kernel file to temporary memory
	VOID* temp_buffer;
	status = ReadFile(kernel_file, &temp_buffer, L"kernel");
	if (EFI_ERROR(status)) {
		return status;
	}

	// 2. read ELF header to obtain [base address] and [required memory space] for kernel
	Elf64_Ehdr* elf_header = (Elf64_Ehdr*)temp_buffer;
	UINT64 kernel_begin_addr, kernel_end_addr;
	CalcLoadAddressRange(elf_header, &kernel_begin_addr, &kernel_end_addr);
	*kernel_base_address = kernel_begin_addr;
	*kernel_memory_size = kernel_end_addr - kernel_begin_addr;

	// 3. allocate memory at [base address] and load kernel "LOAD-segments" appropriately
	status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, REQUIRED_PAGES(*kernel_memory_size), kernel_base_address);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to allocate pages for kernel: %r\n", status);
		goto load_kernel_cleanup;
	}
	CopyLoadSegments(elf_header);

load_kernel_cleanup:
	// 5. free temporary memory
	status = gBS->FreePool(temp_buffer);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to free temporary memory pool at %p: %r\n", temp_buffer, status);
		return status;
	}

	return EFI_SUCCESS;
}

void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last) {
	Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
	*first = MAX_UINT64;
	*last = 0;
	for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) continue;
		*first = MIN(*first, phdr[i].p_vaddr);
		*last = MAX(*last, phdr[i].p_vaddr + phdr[i].p_memsz);
	}
}

void CopyLoadSegments(Elf64_Ehdr* ehdr) {
	Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
	for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) continue;
		
		// copy contents of LOAD segment
		UINT64 seg_addr = (UINT64)ehdr + phdr[i].p_offset;
		CopyMem((VOID*)phdr[i].p_vaddr, (VOID*)seg_addr, phdr[i].p_filesz);

		// add paddings when p_memsz > p_filesz
		UINTN remain_bytes = phdr[i].p_memsz - phdr[i].p_filesz;
		SetMem((VOID*)(phdr[i].p_vaddr + phdr[i].p_filesz), remain_bytes, 0x00);
	}
}

EFI_STATUS ExitBootServices(EFI_HANDLE image_handle, struct MemoryMap* map) {
	EFI_STATUS status;
	status = gBS->ExitBootServices(image_handle, map->map_key);
	if (!EFI_ERROR(status)) return EFI_SUCCESS;

	status = GetMemoryMap(map);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to obtain memory map: %r\n", status);
		return status;
	}

	status = gBS->ExitBootServices(image_handle, map->map_key);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to exit boot services: %r\n", status);
		return status;
	}

	return EFI_SUCCESS;
}

EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
	if (map->buffer == NULL)
		return EFI_BUFFER_TOO_SMALL;

	map->map_size = map->buffer_size;
	return gBS->GetMemoryMap(
		&map->map_size,
		(EFI_MEMORY_DESCRIPTOR*)map->buffer,
		&map->map_key,
		&map->descriptor_size,
		&map->descriptor_version
	);
}

EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root) {
	EFI_STATUS status;
	EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* file_system;

	status = gBS->OpenProtocol(
		image_handle,
		&gEfiLoadedImageProtocolGuid,
		(VOID**)&loaded_image,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
	);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to load protocol(LoadedImage): %r\n", status);
		return status;
	}
	
	status = gBS->OpenProtocol(
		loaded_image->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid,
		(VOID**)&file_system,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
	);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to load protocol(SimpleFileSystem): %r\n", status);
		return status;
	}
	
	status = file_system->OpenVolume(file_system, root);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to open directory(root): %r\n", status);
		return status;
	}

	return EFI_SUCCESS;
}

EFI_STATUS SaveMemoryMap(const struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
	EFI_STATUS status;

	CHAR8 buffer[256];
	CHAR8* header = "Index\tType\tPStart\tPages\tAttrib\n";
	UINTN len = AsciiStrLen(header);
	status = file->Write(file, &len, header);
	if (EFI_ERROR(status)) {
		Print(L"[ERROR]Failed to write memmap file: %r\n", status);
		return status;
	}

	EFI_PHYSICAL_ADDRESS iterator = (EFI_PHYSICAL_ADDRESS)map->buffer;
	unsigned index = 0;
	while (iterator < (EFI_PHYSICAL_ADDRESS)map->buffer + map->map_size) {
		EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iterator;

		len = AsciiSPrint(
			buffer, sizeof(buffer),
			"%u, %-ls, 0x%08lx, 0x%04lx, 0x%05lx\n",
			index, GetMemoryTypeUnicode(desc->Type), desc->PhysicalStart, desc->NumberOfPages, desc->Attribute & 0xffffflu
		);
		status = file->Write(file, &len, buffer);
		if (EFI_ERROR(status)) {
			Print(L"[ERROR]Failed to write memmap file: %r\n", status);
			return status;
		}
		
		iterator += map->descriptor_size; index++;
	}

	return EFI_SUCCESS;
}

const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type) {
	switch (type) {
		case EfiReservedMemoryType: 	return L"EfiReservedMemoryType";
		case EfiLoaderCode: 			return L"EfiLoaderCode";
		case EfiLoaderData: 			return L"EfiLoaderData";
		case EfiBootServicesCode: 		return L"EfiBootServicesCode";
		case EfiBootServicesData: 		return L"EfiBootServicesData";
		case EfiRuntimeServicesCode: 	return L"EfiRuntimeServicesCode";
		case EfiRuntimeServicesData: 	return L"EfiRuntimeServicesData";
		case EfiConventionalMemory: 	return L"EfiConventionalMemory";
		case EfiUnusableMemory: 		return L"EfiUnusableMemory";
		case EfiACPIReclaimMemory: 		return L"EfiACPIReclaimMemory";
		case EfiACPIMemoryNVS: 			return L"EfiACPIMemoryNVS";
		case EfiMemoryMappedIO: 		return L"EfiMemoryMappedIO";
		case EfiMemoryMappedIOPortSpace:return L"EfiMemoryMappedIOPortSpace";
		case EfiPalCode: 				return L"EfiPalCode";
		case EfiPersistentMemory: 		return L"EfiPersistentMemory";
		case EfiMaxMemoryType: 			return L"EfiMaxMemoryType";
		default: 						return L"InvalidMemoryType";
	}
}
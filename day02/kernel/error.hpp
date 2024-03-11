#pragma once

#include <cstdio>
#include <array>

class Error {
public:
	enum Code {
		kSuccess,
		kFull,
		kEmpty,
		kNoEnoughMemory,
		kIndexOutOfRange,
		kHostControllerNotHalted,
		kInvalidSlotID,
		kPortNotConnected,
		kInvalidEndpointNumber,
		kTransferRingNotSet,
		kAlreadyAllocated,
		kNotImplemented,
		kInvalidDescriptor,
		kBufferTooSmall,
		kUnknownDevice,
		kNoCorrespondingSetupStage,
		kTransferFailed,
		kInvalidPhase,
		kUnknownXHCISpeedID,
		kNoWaiter,
		kNoPCIMSI,
		kUnknownPixelFormat,
		kNoSuchTask,
		kInvalidFormat,
		kIsDirectory,
		kNoSuchEntry,
		kFreeTypeError,
		kLastOfCode,
	};
private:
	static constexpr std::array code_str = {
		"kSuccess",
		"kFull",
		"kEmpty",
		"kNoEnoughMemory",
		"kIndexOutOfRange",
		"kHostControllerNotHalted",
		"kInvalidSlotID",
		"kPortNotConnected",
		"kInvalidEndpointNumber",
		"kTransferRingNotSet",
		"kAlreadyAllocated",
		"kNotImplemented",
		"kInvalidDescriptor",
		"kBufferTooSmall",
		"kUnknownDevice",
		"kNoCorrespondingSetupStage",
		"kTransferFailed",
		"kInvalidPhase",
		"kUnknownXHCISpeedID",
		"kNoWaiter",
		"kNoPCIMSI",
		"kUnknownPixelFormat",
		"kNoSuchTask",
		"kInvalidFormat",
		"kIsDirectory",
		"kNoSuchEntry",
		"kFreeTypeError",
	};
	static_assert(kLastOfCode == code_str.size());

public:
#ifndef NO_DETAILED_ERROR_MSG
#define ERR_INIT_FILE_LINE , file{file}, line{line}
#elif
#define ERR_INIT_FILE_LINE
#endif

	Error(Code code, const char* file=nullptr, int line=0) : code{code} ERR_INIT_FILE_LINE {}
	operator bool() const {
		return code != Code::kSuccess;
	}

	Code GetCode() const {
		return code;
	}
	const char* GetName() const {
		return code_str[static_cast<int>(code)];
	}

#ifndef NO_DETAILED_ERROR_MSG
	const char* GetFile() const {
		return file;
	}
	int GetLine() const {
		return line;
	}
#elif
	const char* GetFile() const {
		return "\0";
	}
	int GetLine() const {
		return 0;
	}
#endif

// for uchan's USB driver support (begin)
	Code Cause() const { return code; }
	const char* Name() const { return GetName(); }
	const char* File() const { return GetFile(); }
	int Line() const { return GetLine(); }
// for uchan's USB driver support (end)

private:
	Code code;
#ifndef NO_DETAILED_ERROR_MSG
	const char* file;
	int line;
#endif
};

#ifndef NO_DETAILED_ERROR_MSG
#define MakeError(code) Error((code), __FILE__, __LINE__)
#define MAKE_ERROR(code) Error((code), __FILE__, __LINE__)
#elif
#define makeError(code) Error((code))
#define MAKE_ERROR(code) Error((code))
#endif

template <class T>
struct Optional {
	union {
		T value;
		Error error;
	};
	bool has_value;

	Optional(T value) : value(value), has_value(true) {}
	Optional(Error error) : error(error), has_value(false) {}
	T& operator*() { return value; };
	const T& operator*() const { return value; };
};

// for uchan's USB driver support
template <class T>
struct WithError {
	T value;
	Error error;
};

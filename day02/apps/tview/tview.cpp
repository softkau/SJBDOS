#if 1
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <tuple>
#include <unistd.h>
#include <vector>
#include "../syscall.h"

// #@@range_begin(map_file)
std::tuple<int, char*, size_t> MapFile(const char* filepath) {
  SyscallResult res = SyscallOpenFile(filepath, O_RDONLY);
  if (res.error) {
    fprintf(stderr, "%s: %s\n", strerror(res.error), filepath);
    exit(1);
  }

  const int fd = res.value;
  size_t filesize;
  res = SyscallMapFile(fd, &filesize, 0);
  if (res.error) {
    fprintf(stderr, "%s\n", strerror(res.error));
    exit(1);
  }

  return {fd, reinterpret_cast<char*>(res.value), filesize};
}
// #@@range_end(map_file)

// #@@range_begin(open_textwindow)
uint64_t OpenTextWindow(int w, int h, const char* title) {
  SyscallResult res = SyscallOpenWindow(8 + 8*w, 28 + 16*h, 10, 10, title);
  if (res.error) {
    fprintf(stderr, "%s\n", strerror(res.error));
    exit(1);
  }
  const uint64_t layer_id = res.value;

  auto fill_rect = [layer_id](int x, int y, int w, int h, uint32_t c) {
    SyscallWinFillRectangle(layer_id, x, y, w, h, c);
  };
  fill_rect(3,       23,        1 + 8*w, 1,        0x666666);
  fill_rect(3,       24,        1,       1 + 16*h, 0x666666);
  fill_rect(4,       25 + 16*h, 1 + 8*w, 1,        0xcccccc);
  fill_rect(5 + 8*w, 24,        1,       1 + 16*h, 0xcccccc);

  return layer_id;
}
// #@@range_end(open_textwindow)

// #@@range_begin(find_lines)
using LinesType = std::vector<std::pair<const char*, size_t>>;

LinesType FindLines(const char* p, size_t len) {
  LinesType lines;
  const char* end = p + len;

  auto next_lf = [end](const char* s) {
    while (s < end && *s != '\n') {
      ++s;
    }
    return s;
  };

  const char* lf = next_lf(p);
  while (lf < end) {
    lines.push_back({p, lf - p});
    p = lf + 1;
    lf = next_lf(p);
  }
  if (p < end) {
    lines.push_back({p, end - p});
  }

  return lines;
}
// #@@range_end(find_lines)

// #@@range_begin(count_utf8size)
int CountUTF8Size(uint8_t c) {
  if (c < 0x80) {
    return 1;
  } else if (0xc0 <= c && c < 0xe0) {
    return 2;
  } else if (0xe0 <= c && c < 0xf0) {
    return 3;
  } else if (0xf0 <= c && c < 0xf8) {
    return 4;
  }
  return 0;
}
// #@@range_end(count_utf8size)

// #@@range_begin(copy_utf8string)
void CopyUTF8String(char* dst, size_t dst_size,
                    const char* src, size_t src_size,
                    int w, int tab) {
  int x = 0;

  const auto src_end = src + src_size;
  const auto dst_end = dst + dst_size;
  while (*src) {
    if (*src == '\t') {
      int spaces = tab - (x % tab);
      if (dst + spaces >= dst_end) {
        break;
      }
      memset(dst, ' ', spaces);
      ++src;
      dst += spaces;
      x += spaces;
      continue;
    }

    if (static_cast<uint8_t>(*src) < 0x80) {
      x += 1;
    } else {
      x += 2;
    }
    if (x >= w) {
      break;
    }

    int c = CountUTF8Size(*src);
    if (src + c > src_end || dst + c >= dst_end) {
      break;
    }
    memcpy(dst, src, c);
    src += c;
    dst += c;
  }

  *dst = '\0';
}
// #@@range_end(copy_utf8string)

// #@@range_begin(draw_lines)
void DrawLines(const LinesType& lines, int start_line,
               uint64_t layer_id, int w, int h, int tab) {
  char buf[1024];
  SyscallWinFillRectangle(layer_id, 4, 24, 8*w, 16*h, 0xffffff);

  for (int i = 0; i < h; ++i) {
    int line_index = start_line + i;
    if (line_index < 0 || lines.size() <= line_index) {
      continue;
    }
    const auto [ line, line_len ] = lines[line_index];
    CopyUTF8String(buf, sizeof(buf), line, line_len, w, tab);
    SyscallWinWriteString(layer_id, 4, 24 + 16*i, 0x000000, buf);
  }
}
// #@@range_end(draw_lines)

// #@@range_begin(wait_event)
std::tuple<bool, int> WaitEvent(int h) {
  AppEvent events[1];
  while (true) {
    auto [ n, err ] = SyscallReadEvent(events, 1);
    if (err) {
      fprintf(stderr, "ReadEvent failed: %s\n", strerror(err));
      return {false, 0};
    }
    if (events[0].type == AppEvent::kQuit) {
      return {true, 0};
    } else if (events[0].type == AppEvent::kKeyPush &&
               events[0].arg.keypush.press) {
      return {false, events[0].arg.keypush.keycode};
    }
  }
}
// #@@range_end(wait_event)

// #@@range_begin(update_startline)
bool UpdateStartLine(int* start_line, int height, size_t num_lines) {
  while (true) {
    const auto [ quit, keycode ] = WaitEvent(height);
    if (quit) {
      return quit;
    }
    if (num_lines < height) {
      continue;
    }

    int diff;
    switch (keycode) {
    case 75: diff = -height/2; break; // PageUp
    case 78: diff =  height/2; break; // PageDown
    case 81: diff =  1;        break; // DownArrow
    case 82: diff = -1;        break; // UpArrow
    default:
      continue;
    }

    if ((diff < 0 && *start_line == 0) ||
        (diff > 0 && *start_line == num_lines - height)) {
      continue;
    }

    *start_line += diff;
    if (*start_line < 0) {
      *start_line = 0;
    } else if (*start_line > num_lines - height) {
      *start_line = num_lines - height;
    }
    return false;
  }
}
// #@@range_end(update_startline)

// #@@range_begin(main)
extern "C" void main(int argc, char** argv) {
  auto print_help = [argv](){
    fprintf(stderr,
            "Usage: %s [-w WIDTH] [-h HEIGHT] [-t TAB] <file>\n",
            argv[0]);
  };

  int opt;
  int width = 80, height = 20, tab = 8;
  while ((opt = getopt(argc, argv, "w:h:t:")) != -1) {
    switch (opt) {
    case 'w': width = atoi(optarg); break;
    case 'h': height = atoi(optarg); break;
    case 't': tab = atoi(optarg); break;
    default:
      print_help();
      exit(1);
    }
  }
  if (optind >= argc) {
    print_help();
    exit(1);
  }

  const char* filepath = argv[optind];
  const auto [ fd, content, filesize ] = MapFile(filepath);

  const char* last_slash = strrchr(filepath, '/');
  const char* filename = last_slash ? &last_slash[1] : filepath;
  const auto layer_id = OpenTextWindow(width, height, filename);

  const auto lines = FindLines(content, filesize);

  int start_line = 0;
  while (true) {
    DrawLines(lines, start_line, layer_id, width, height, tab);
    if (UpdateStartLine(&start_line, height, lines.size())) {
      break;
    }
  }

  SyscallCloseWindow(layer_id);
  exit(0);
}
// #@@range_end(main)


#else
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include "../syscall.h"

int CountUTF8Size(uint8_t c) {
	if (c < 0x80) return 1;
	else if (0xc0 <= c && c < 0xe0) return 2;
	else if (0xe0 <= c && c < 0xf0) return 3;
	else if (0xf0 <= c && c < 0xf8) return 4;
	else return 1;
}


std::pair<char32_t, int> ConvertUTF8to32(const char* u8) {
	switch (CountUTF8Size(u8[0])) {
		case 1: return {
			static_cast<char32_t>(u8[0]),
			1
		};
		case 2: return {
			(static_cast<char32_t>(u8[0]) & 0b0001'1111) << 6 |
			(static_cast<char32_t>(u8[1]) & 0b0011'1111) << 0,
			2
		};
		case 3: return {
			(static_cast<char32_t>(u8[0]) & 0b0000'1111) << 12 |
			(static_cast<char32_t>(u8[1]) & 0b0011'1111) <<  6 |
			(static_cast<char32_t>(u8[2]) & 0b0011'1111) <<  0,
			3
		};
		case 4: return {
			(static_cast<char32_t>(u8[0]) & 0b0000'0111) << 18 |
			(static_cast<char32_t>(u8[1]) & 0b0011'1111) << 12 |
			(static_cast<char32_t>(u8[2]) & 0b0011'1111) <<  6 |
			(static_cast<char32_t>(u8[3]) & 0b0011'1111) <<  0,
			4
		};
		default: return { 0, 1 };
	}
}

struct MappedFile {
	int fd;
	void* content;
	size_t size;
};

MappedFile MapFile(const char* path) {
	MappedFile mf;
	mf.fd = open(path, O_RDONLY);
	if (mf.fd < 0) {
		return { mf.fd, nullptr, 0 };
	}

	auto [ res, err ] = SyscallMapFile(mf.fd, &mf.size, 0);
	if (err) {
		return { mf.fd, nullptr, mf.size };
	}
	mf.content = reinterpret_cast<void*>(res);
	return mf;
}

char title_buf[1024];
constexpr int top_left_margin_x = 4;
constexpr int top_left_margin_y = 24;
constexpr int margin = 4;

int bg_w = 0;
int bg_h = 0;

void DrawTextbox(uint64_t layer_id_flags, int x, int y, int w, int h) {
	auto fill_rect = [layer_id_flags](int x, int y, int w, int h, uint32_t c) {
		SyscallWinFillRect(layer_id_flags | LAYER_NO_DRAW, x, y, w, h, c);
	};

	constexpr uint32_t bg_color     = 0xffffff;
	constexpr uint32_t shadow_color = 0xc6c6c6;
	constexpr uint32_t light_color  = 0x848484;

	bg_w = w-2;
	bg_h = h-2;

	fill_rect(x+1, y+1, bg_w, bg_h, bg_color);
	fill_rect(x, y, w, 1, shadow_color);
	fill_rect(x, y, 1, h, shadow_color);
	fill_rect(x, y+h, w, 1, light_color);
	fill_rect(x+w, y, 1, h, light_color);
	SyscallWinRedraw(layer_id_flags);
}

uint64_t OpenTextWindow(int w, int h, const char* filename) {
	int n = strlen(filename);
	if (n > w - 3) {
		strncpy(title_buf, filename, w - 6);
		title_buf[w - 6] = '.';
		title_buf[w - 5] = '.';
		title_buf[w - 4] = '.';
		title_buf[w - 3] = '\0';

	} else {
		strcpy(title_buf, filename);
	}

	int win_w = w * 8  + top_left_margin_x + margin + 2;
	int win_h = h * 16 + top_left_margin_y + margin + 2;

	auto [layerID, err] = SyscallOpenWindow(win_w, win_h, 10, 10, filename);
	if (err) {
		return 0;
	}
	DrawTextbox(layerID,
		top_left_margin_x,
		top_left_margin_y,
		win_w - top_left_margin_x - margin,
		win_h - top_left_margin_y - margin
	);

	return layerID;
}

std::vector<const char*> FindLines(const char* content, size_t file_size) {
	std::vector<const char*> lines;
	
	bool newline = false;
	const char* prv = content;
	for (size_t i = 0; i < file_size; i++) {
		if (newline) {
			lines.push_back(prv);
			newline = false;
			prv = content + i;
		}
		if (content[i] == '\n') {
			newline = true;
		}
	}
	lines.push_back(prv);

	return lines;
}

void DrawLines(const std::vector<const char*>& lines, int start_line, uint64_t layerID, int w, int h, int tab) {
	constexpr uint32_t bg_color = 0xffffff;
	auto get_render_line = [w](const char* line) {
		int calc_w = 0;
		int i = 0;
		while (line[i] && line[i] != '\n') {
			int inc_w = (unsigned)line[i] < 0x80 ? 1 : 2;
			if (calc_w + inc_w > w) {
				return i;
			}
			calc_w += inc_w;
			i += CountUTF8Size(line[i]);
		}
		return i;
	};

	char u8buf[256] = {};

	int line_number = 0;
	SyscallWinFillRect(layerID | LAYER_NO_DRAW, top_left_margin_x+1, top_left_margin_y+1, bg_w, bg_h, bg_color);
	for (const auto& line : lines) {
		if (!line[0] || line[0] == '\n') {
			line_number++;
			continue;
		}

		int i = 0;
		while (line[i] && line[i] != '\n') {
			int n = get_render_line(line + i);
			strncpy(u8buf, line + i, n);
			u8buf[n] = 0x00;

			if (line_number >= start_line && line_number < start_line + h) {
				SyscallWinWriteString(layerID | LAYER_NO_DRAW, top_left_margin_x + 1, top_left_margin_y + 1 + line_number * 16, 0x000000, u8buf);
			}
			i += n;
			line_number++;
		}
	}
	SyscallWinRedraw(layerID);
}

bool UpdateStartLine(int* start_line, int h, size_t line_count) {
	AppEvent event;
	while (true) {
		auto [ n, err ] = SyscallReadEvent(&event, 1);
		if (err) {
			fprintf(stderr, "error occurred while reading an evnet: %s\n", strerror(err));
			return true;
		}
		if (event.type == AppEvent::kQuit) {
			return true;	
		}
		else if (event.type != AppEvent::kKeyPush) continue;
		
		if (event.arg.keypush.press) {
			switch (event.arg.keypush.keycode) {
			case 33: if (*start_line > 0) (*start_line)--; SyscallLogString(kWarn, "PG UP"); break; // Page Up
			case 34: if (*start_line + h < (int)line_count) (*start_line)++; SyscallLogString(kWarn, "PG DN"); break; // Page Down
			default: printf("kp:%u\n", event.arg.keypush.keycode); break;
			}
		}
		return false;
	}
}

extern "C" void main(int argc, char** argv) {
	auto print_help = [argv]() {
		fprintf(stderr, "Usage: %s [-w <width>] [-h <height>] [-t <tab>] <file>\n", argv[0]);
	};

	int opt;
	int width = 80;
	int height = 20;
	int tab = 4;

	while ((opt = getopt(argc, argv, "w:h:t:")) != -1) {
		switch (opt) {
			case 'w': width = atoi(optarg); break;
			case 'h': height = atoi(optarg); break;
			case 't': tab = atoi(optarg); break;
			default: print_help(); exit(1);
		}
	}
	if (optind >= argc) {
		print_help();
		exit(1);
	}

	const char* filepath = argv[optind];
	const auto [ fd, content, filesize ] = MapFile(filepath);

	const char* last_slash = strrchr(filepath, '/');
	const char* filename = last_slash ? last_slash + 1 : filepath;
	const auto layerID = OpenTextWindow(width, height, filename);

	std::vector<const char*> lines = FindLines((const char*)content, filesize);
	int start_line = 0;
	while (true) {
		DrawLines(lines, start_line, layerID, width, height, tab);
		if (UpdateStartLine(&start_line, height, lines.size())) {
			break;
		}
	}

	SyscallCloseWindow(layerID);
	exit(0);
}

#endif
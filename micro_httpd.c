/* micro_httpd - really small HTTP server
 *
 * Copyright ©1999~2014 by Jef Poskanzer <jef@mail.acme.com>.
 * Copyright ©2015 by Marcos Del Sol Vives <socram@protonmail.ch>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#define SERVER_NAME "micro_httpd"
#define SERVER_URL "https://github.com/socram8888/micro_httpd"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

/* Forwards. */
void send_error(int status, const char * title, const char * extra_header, const char * text);
void send_headers(int status, const char * title, const char * extra_header, const char * mime_type, off_t length, time_t mod);
const char * get_mime_type(const char * name);
size_t strdecode(const char * from, char * to);
int hexit(char c);
ssize_t strencode(const char * from, char * to, size_t max);
size_t read_http_line(char * line, size_t max, FILE * file);
void send_errno();
void file_details(const char * dir, const char * name);
void do_file(const char * file, const struct stat * sb);
void do_directory(const char * file, const struct stat * sb);

int main(int argc, char** argv) {
	if (argc != 2) {
		send_error(500, "Internal Error", NULL, "Configuration error - no directory specified.");
	}

	if (chdir(argv[1]) < 0) {
		send_error(500, "Internal Error", NULL, "Configuration error - directory not accessible.");
	}

#	define MAX_LINE_LEN 8192
	char line[MAX_LINE_LEN];
	if (read_http_line(line, sizeof(line), stdin) == 0) {
		send_error(400, "Bad Request", NULL, "No request found.");
	}

	char method[MAX_LINE_LEN];
	char path[MAX_LINE_LEN];
	char protocol[MAX_LINE_LEN];
	if (sscanf(line, "%[^ ] %[^ ] %[^ ]", method, path, protocol) != 3) {
		send_error(400, "Bad Request", NULL, "Can't parse request.");
	}

	while (read_http_line(line, sizeof(line), stdin) > 0);

	if (strcasecmp(method, "get") != 0) {
		send_error(501, "Not Implemented", NULL, "That method is not implemented.");
	}

	if (path[0] != '/') {
		send_error(400, "Bad Request", NULL, "Bad filename.");
	}

	char * file = path + 1;
	size_t filelen = strdecode(file, file);
	if (filelen == 0) {
		file = "./";
		filelen = 2;
	} else {
		if (
			file[0] == '/' ||
			strcmp(file, "..") == 0 ||
			strncmp(file, "../", 3) == 0 ||
			strstr(file, "/../") != NULL ||
			strcmp(file + filelen - 3, "/..") == 0
		) {
			send_error(400, "Bad Request", NULL, "Illegal filename.");
		}
	}

	struct stat sb;
	if (stat(file, &sb) < 0) {
		send_errno();
	}

	if (!S_ISDIR(sb.st_mode)) {
		do_file(file, &sb);
		return 0;
	}

	if (file[filelen - 1] != '/') {
		char location[MAX_LINE_LEN];
		snprintf(location, sizeof(location), "Location: %s/", path);
		send_error(301, "Moved Permanently", location, "Directories must end with a slash.");
	}

	char indexfile[MAX_LINE_LEN];
	snprintf(indexfile, sizeof(indexfile), "%sindex.html", file);
	if (stat(indexfile, &sb) >= 0) {
		do_file(indexfile, &sb);
		return 0;
	}

	do_directory(file, &sb);
	return 0;
}

void do_file(const char * file, const struct stat * sb) {
	FILE * fp = fopen(file, "r");
	if (fp == NULL) {
		send_errno();
	}

	send_headers(200, "Ok", NULL, get_mime_type(file), sb->st_size, sb->st_mtime);

	char buf[8192];
	size_t read;
	while ((read = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (!fwrite(buf, read, 1, stdout)) {
			break;
		}
	}
}

void do_directory(const char * file, const struct stat * sb) {
	int n;
	struct dirent **dl;

	send_headers(200, "Ok", NULL, "text/html", -1, sb->st_mtime);
	printf("\
<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n\
<html>\n\
	<head>\n\
		<meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\n\
		<title>Index of %s</title>\n\
	</head>\n\
	<body bgcolor=\"#99cc99\">\n\
		<h4>Index of %s</h4>\n\
		<pre>\n", file, file);
	n = scandir(file, &dl, NULL, alphasort);
	if (n < 0) {
		perror("scandir");
	} else {
		for (int i = 0; i < n; ++i) {
			file_details(file, dl[i]->d_name);
		}
	}

	printf("\
		</pre>\n\
		<hr>\n\
		<address><a href=\"%s\">%s</a></address>\n\
	</body>\n\
</html>\n", SERVER_URL, SERVER_NAME);
}

void file_details(const char * dir, const char * name) {
	static char encoded_name[1000];
	static char path[2000];
	struct stat sb;
	char timestr[16];

	if (strencode(name, encoded_name, sizeof(encoded_name)) < 0) {
		perror("strencode");
	}

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (lstat(path, &sb) < 0) {
		printf("<a href=\"%s\">%-32.32s</a>	???\n", encoded_name, name);
	} else {
		strftime(timestr, sizeof(timestr), "%d%b%Y %H:%M", localtime(&sb.st_mtime));
		printf("<a href=\"%s\">%-32.32s</a>	%15s %14lld\n", encoded_name, name, timestr, (long long) sb.st_size);
	}
}


void send_errno() {
	switch (errno) {
		case ENOENT:
			send_error(404, "Not Found", NULL, "File not found.");

		case EACCES:
			send_error(403, "Forbidden", NULL, "Access denied.");
	}

	send_error(500, "Internal Server Error", NULL, "An unexpected error occurred while trying to handle your request.");
}

void send_error(int status, const char * title, const char * extra_header, const char * text) {
	send_headers(status, title, extra_header, "text/html", -1, -1);

	printf("\
<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n\
<html>\n\
	<head>\n\
		<meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\n\
		<title>%d %s</title>\n\
	</head>\n\
	<body bgcolor=\"#cc9999\">\n\
		<h4>%d %s</h4>\n", status, title, status, title);
		printf("%s\n", text);
		printf("\
		<hr>\n\
		<address><a href=\"%s\">%s</a></address>\n\
	</body>\n\
</html>\n", SERVER_URL, SERVER_NAME);

	exit(1);
}

void send_headers(int status, const char * title, const char * extra_header, const char * mime_type, off_t length, time_t mod) {
	char timebuf[100];

	printf("%s %d %s\r\n", PROTOCOL, status, title);
	printf("Server: %s\r\n", SERVER_NAME);

	time_t now = time(NULL);
	strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
	printf("Date: %s\r\n", timebuf);

	if (extra_header != NULL) {
		printf("%s\r\n", extra_header);
	}

	if (mime_type != NULL) {
		printf("Content-Type: %s\r\n", mime_type);
	}

	if (length >= 0) {
		printf("Content-Length: %lld\r\n", (long long) length);
	}

	if (mod != (time_t) -1) {
		strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&mod));
		printf("Last-Modified: %s\r\n", timebuf);
	}

	printf("Connection: close\r\n");
	printf("\r\n");
}

const char * get_mime_type(const char * name) {
	char * dot = strrchr(name, '.');
	if (dot == NULL)
		return NULL;

	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=UTF-8";
	if (strcmp(dot, ".xhtml") == 0 || strcmp(dot, ".xht") == 0)
		return "application/xhtml+xml; charset=UTF-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".xml") == 0 || strcmp(dot, ".xsl") == 0)
		return "text/xml; charset=UTF-8";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";

	return NULL;
}

int hexit(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}

	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 0xA;
	}

	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 0xA;
	}

	return -1;
}

size_t strdecode(const char * from, char * to) {
	char * start = to;

	while (*from) {
		int x1, x2;

		if (from[0] == '%' && (x1 = hexit(from[1])) >= 0 && (x2 = hexit(from[2])) >= 0) {
			*to = hexit(from[1]) * 16 + hexit(from[2]);
			from += 3;
		} else {
			*to = *from;
			from++;
		}

		to++;
	}
	*to = '\0';

	return to - start;
}

ssize_t strencode(const char * from, char * to, size_t max) {
	char * start = to;

	while (*from) {
		if (isalnum(*from) || strchr("/_.-~", *from) != NULL) {
			if (max == 0) {
				return -1;
			}

			*to = *from;
			to++;
			max--;
		} else {
			if (max < 3) {
				return -1;
			}

			sprintf(to, "%%%02x", (int) *from);
			to += 3;
			max -= 3;
		}

		from++;
	}

	if (max == 0) {
		return -1;
	}
	*to = '\0';

	return to - start;
}

size_t read_http_line(char * line, size_t max, FILE * file) {
	size_t read = 0;

	while (1) {
		if (read == max) {
			send_error(413, "Entity Too Large", NULL, "Request line too long.");
		}

		int c = fgetc(file);
		switch (c) {
			case '\r':
				c = fgetc(file);
				if (c != '\n') {
					send_error(400, "Bad Request", NULL, "Unexpected byte sequence in headers.");
				}

			case EOF:
			case '\n':
				line[read] = '\0';
				return read;

			default:
				line[read++] = c;
		}
	}
}

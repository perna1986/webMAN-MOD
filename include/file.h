#define SC_FS_LINK						(810)
#define SC_FS_MOUNT 					(837)
#define SC_FS_UMOUNT					(838)
#define SC_FS_DISK_FREE 				(840)

#define SC_BDVD_DECRYPT 				(36)

#define NO_MSG							NULL

int64_t file_copy(char *file1, char *file2, u64 maxbytes);

static bool copy_in_progress = false;
static bool dont_copy_same_size = true; // skip copy the file if it already exists in the destination folder with the same file size
static bool allow_sc36 = true; // used to skip decrypt dev_bdvd files in file_copy function if it's called from folder_copy

static u32 copied_count = 0;

#define COPY_WHOLE_FILE		0
#define SAVE_ALL			0
#define APPEND_TEXT			(-0xADD0ADD0ADD000ALL)
#define DONT_CLEAR_DATA		-1
#define RECURSIVE_DELETE	2 // don't check for ADMIN/USER

#define DEV_NTFS		"/dev_nt"

static void enable_dev_blind(const char *msg);
static sys_addr_t g_sysmem = NULL;

enum scan_operations
{
	SCAN_LIST   = 0,
	SCAN_DELETE = 1,
	SCAN_COPY   = 2,
	SCAN_MOVE   = 3,
	SCAN_RENAME = 4,
	SCAN_COPYBK = 5,	// rename source to source + .bak after copy
	SCAN_UNLOCK_SAVE = 6,
	SCAN_LIST_SIZE = 7,
	SCAN_TRUNCATE  = 8
};

#ifdef USE_NTFS

static bool is_ntfs_path(const char *path)
{
	return islike(path, DEV_NTFS);
}

static void unmount_all_ntfs_volumes(void)
{
	if(mounts && (mountCount > 0))
		for(u8 u = 0; u < mountCount; u++) ntfsUnmount(mounts[u].name, 1);

	if(mounts) free(mounts);
}

static void mount_all_ntfs_volumes(void)
{
	unmount_all_ntfs_volumes();
	mountCount = ntfsMountAll(&mounts, NTFS_SU | NTFS_FORCE );
	if(mountCount <= 0) {mountCount = NTFS_UNMOUNTED;}
}

static u32 ftp_ntfs_transfer_in_progress = 0;

static void check_ntfs_volumes(void)
{
	root_check = false;

	if((mountCount > 0) && (!ftp_ntfs_transfer_in_progress))
	{
		DIR_ITER *pdir; char path[40];
		for(int i = 0; i < mountCount; i++)
		{
			snprintf(path, sizeof(path), "%s:/", mounts[i].name);
			pdir = ps3ntfs_diropen(path);
			if(pdir) ps3ntfs_dirclose(pdir); else { mountCount = NTFS_UNMOUNTED; break; }
		}
	}

	if(mountCount <= 0)
		for(u8 retry = 0; retry < 2; retry++)
		{
			mount_all_ntfs_volumes();
			if(mountCount > 0) break;
			sys_ppu_thread_sleep(2);
		}
}

static DIR_ITER *ps3ntfs_opendir(char *path)
{
	if(mountCount <= 0) mount_all_ntfs_volumes();

	path[10] = ':';
	if(path[11] != '/') {path[11] = '/', path[12] = 0;}
	return ps3ntfs_diropen(path + 5);  // /dev_ntfs1v -> ntfs1:
}
#endif

static void filepath_check(char *file)
{
	if((file[5] == 'u' && islike(file, "/dev_usb"))
#ifdef USE_NTFS
	|| (file[5] == 'n' && is_ntfs_path(file))
#endif
	)
	{
		// remove invalid chars
		u16 n = 11;
		for(u16 c = 11; file[c]; c++)
		{
			if(file[c] == '\\') file[c] = '/';
			if(!strchr("\"<|>:*?", file[c])) file[n++] = file[c];
		}
		file[n] = 0;
	}
#ifdef USE_NTFS
	if(is_ntfs_path(file)) {file[10] = ':'; if(mountCount == NTFS_UNMOUNTED) check_ntfs_volumes();}
#endif
}

static void check_path_alias(char *param)
{
	if(islike(param, "/dev_blind")) enable_dev_blind(NULL);

	if(*param && not_exists(param))
	{
		if(!islike(param, "/dev_") && !islike(param, "/net"))
		{
			char path[STD_PATH_LEN];
			int len = snprintf(path, STD_PATH_LEN - 1, "%s", (*param == '/') ? param + 1 : param);
			char *wildcard = strchr(path, '*'); if(wildcard) *wildcard = 0;
			if((len == 4) && path[3] == '/') path[3] = 0; // normalize path
			if(IS(path, "pkg"))  {sprintf(param, DEFAULT_PKG_PATH);} else
			if(IS(path, "xml"))  {*path = 0;} else
			if(IS(path, "xmb"))  {enable_dev_blind(NULL); sprintf(param, "/dev_blind/vsh/resource/explore/xmb");} else
			if(IS(path, "res"))  {enable_dev_blind(NULL); sprintf(param, "/dev_blind/vsh/resource");} else
			if(IS(path, "mod"))  {enable_dev_blind(NULL); sprintf(param, "/dev_blind/vsh/module");} else
			if(IS(path, "cov"))  {sprintf(param, "%s/covers", MM_ROOT_STD);} else
			if(IS(path, "cvr"))  {sprintf(param, "%s/covers_retro/psx", MM_ROOT_STD);} else
			if(islike(path, "res/"))   {sprintf(param, "/dev_blind/vsh/resource/%s", path + 4);} else
			if(*html_base_path == '/') {snprintf(param, HTML_RECV_LAST, "%s/%s", html_base_path, path);} // use html path (if path is omitted)

			if(not_exists(param))      {snprintf(param, HTML_RECV_LAST, "%s/%s", HTML_BASE_PATH, path);} // try HTML_BASE_PATH
			if(not_exists(param))      {snprintf(param, HTML_RECV_LAST, "%s/%s", webman_config->home_url, path);} // try webman_config->home_url
			if(not_exists(param))      {snprintf(param, HTML_RECV_LAST, "%s%s",  HDD0_GAME_DIR, path);} // try /dev_hdd0/game
			if(not_exists(param))      {snprintf(param, HTML_RECV_LAST, "%s%s", _HDD0_GAME_DIR, path);} // try /dev_hdd0//game
			if(not_exists(param))
			{
				for(u8 i = 0; i < (MAX_DRIVES + 1); i++)
				{
					if(i == NET) i = NTFS + 1;
					snprintf(param, HTML_RECV_LAST, "%s/%s", drives[i], path);
					if(file_exists(param)) break;
				}
			} // try hdd0, usb0, usb1, etc.
			if(not_exists(param)) {snprintf(param, HTML_RECV_LAST, "%s/%s", "/dev_hdd0/tmp", path);} // try hdd0
			if(wildcard) {*wildcard = '*'; strcat(param, wildcard);}
		}
	}
}


static u64 get_free_space(const char *dev_name)
{
#ifdef USE_NTFS
	if(is_ntfs_path(dev_name))
	{
		char tmp[8];
		strncpy(tmp, dev_name + 5, 8); tmp[5] = ':', tmp[7] = 0;

		struct statvfs vbuf;
		ps3ntfs_statvfs(tmp, &vbuf);
		return ((u64)vbuf.f_bfree * (u64)vbuf.f_bsize);
	}
#endif
	if(!islike(dev_name, "/dev_")) return 0;

	u64 freeSize = 0, devSize = 0;

	{system_call_3(SC_FS_DISK_FREE, (u64)(u32)(dev_name), (u64)(u32)&devSize, (u64)(u32)&freeSize);}
	return freeSize;
/*
	u32 blockSize;
	u64 freeSize;

	if(cellFsGetFreeSize(dev_name, &blockSize, &freeSize)  == CELL_FS_SUCCEEDED) return (freeSize * blockSize);
	return 0;
*/
}

static bool isDir(const char* path)
{
#ifdef USE_NTFS
	if(is_ntfs_path(path))
	{
		char tmp[STD_PATH_LEN];
		strcpy(tmp, path); tmp[10] = ':';
		struct stat bufn;
		return ((ps3ntfs_stat(tmp + 5, &bufn) >= 0) && (bufn.st_mode & S_IFDIR));
	}
#endif

	struct CellFsStat s;
	if(cellFsStat(path, &s) == CELL_FS_SUCCEEDED)
		return ((s.st_mode & CELL_FS_S_IFDIR) != 0);
	else
		return 0;
}

#ifdef COBRA_ONLY
static bool is_app_dir(const char *path, const char *app_name)
{
	char eboot[STD_PATH_LEN];
	sprintf(eboot, "%s/%s/USRDIR/EBOOT.BIN", path, app_name);
	return file_exists(eboot);
}
#endif

static s64 file_ssize(const char *path)
{
#ifdef USE_NTFS
	if(is_ntfs_path(path))
	{
		char tmp[STD_PATH_LEN];
		strcpy(tmp, path); tmp[10] = ':';
		struct stat bufn;
		if(ps3ntfs_stat(tmp + 5, &bufn) < 0) return FAILED;
		return bufn.st_size;
	}
#endif

	struct CellFsStat s;
	if(cellFsStat(path, &s) != CELL_FS_SUCCEEDED) return FAILED;
	return s.st_size;
}

#ifndef LITE_EDITION
static u64 file_size(const char *path)
{
	s64 fs = file_ssize(path);
	if(fs <= FAILED) return 0;
	return fs;
}
#endif

static bool file_exists(const char *path)
{
	return (file_ssize(path) >= 0);
}

static bool not_exists(const char *path)
{
	return (file_ssize(path) <= FAILED);
}

static char *get_ext(const char *path)
{
	int plen = strlen(path) - 4;
	if(plen < 0) plen = 0;
	else if(path[plen + 1] == '.') plen++;
	else if(path[plen + 2] == '.') plen+=2;
	return (char *)(path + plen);
}

static char *get_filename(const char *path)
{
	return strrchr(path, '/'); // return with slash
}

static bool is_ext(const char *path, const char *ext)
{
	return !extcasecmp(path, ext, 4);
}

#ifdef COBRA_ONLY
static bool change_ext(char *filename, int num_ext, const char *file_ext[])
{
	int flen = strlen(filename) - 4;
	for(u8 e = 0; e < num_ext; e++)
	{
		sprintf(filename + flen, "%s", file_ext[e]);
		if(file_exists(filename)) return true;
	}
	return false;
}

static void change_cue2iso(char *cue_file)
{
	if(is_ext(cue_file, ".cue") || is_ext(cue_file, ".ccd"))
	{
		const char *iso_ext[8] = {".bin", ".iso", ".img", ".mdf", ".BIN", ".ISO", ".IMG", ".MDF"};
		change_ext(cue_file, 8, iso_ext);
	}
}

static bool is_iso_0(const char *filename)
{
	#ifdef MOUNT_PNG
	if(!extcasecmp(filename, ".0.PNG", 6)) return true;
	#endif
	return !extcasecmp(filename, ".iso.0", 6);
}

#define check_ps3_game(path)
#else
static void check_ps3_game(char *path)
{
	char *p = strstr(path, "/PS3_GAME");
	if(p)
	{
		p[6] = 'M', p[7] = '0', p[8] = '1';  // PS3_GM01
		if(file_exists(path)) return;
		p[6] = 'A', p[7] = 'M', p[8] = 'E';  // PS3_GAME
	}
}
#endif

#if defined(COPY_PS3) || defined(PKG_HANDLER) || defined(MOUNT_GAMEI)
static void mkdir_tree(char *path)
{
	size_t path_len = strlen(path);
#ifdef USE_NTFS
	if(is_ntfs_path(path))
	{
		path[10] = ':';
		for(u16 p = 12; p < path_len; p++)
			if(path[p] == '/') {path[p] = NULL; ps3ntfs_mkdir(path + 5, DMODE); path[p] = '/';}
	}
	else
#endif
	{
		for(u16 p = 12; p < path_len; p++)
			if(path[p] == '/') {path[p] = NULL; cellFsMkdir(path, DMODE); path[p] = '/';}
	}
}
#endif

static void mkdirs(char *param)
{
	cellFsMkdir(TMP_DIR, DMODE);
	cellFsMkdir(WMTMP, DMODE);
	cellFsMkdir("/dev_hdd0/packages", DMODE);
	//cellFsMkdir("/dev_hdd0/GAMES",  DMODE);
	//cellFsMkdir("/dev_hdd0/PS3ISO", DMODE);
	//cellFsMkdir("/dev_hdd0/DVDISO", DMODE);
	//cellFsMkdir("/dev_hdd0/BDISO",  DMODE);
	//cellFsMkdir("/dev_hdd0/PS2ISO", DMODE);
	//cellFsMkdir("/dev_hdd0/PSXISO", DMODE);
	//cellFsMkdir("/dev_hdd0/PSPISO", DMODE);
	#ifdef MOUNT_ROMS
	cellFsMkdir("/dev_hdd0/ROMS", DMODE);
	#endif

	sprintf(param, "/dev_hdd0");
	for(u8 i = 0; i < 9; i++)
	{
		if(i == 1 || i == 7) continue; // skip /GAMEZ & /PSXGAMES
		sprintf(param + 9 , "/%s", paths[i]);
		cellFsMkdir(param, DMODE);
	}

	param[9] = NULL; // <- return /dev_hdd0
}

size_t read_file(const char *file, char *data, const size_t size, s32 offset)
{
	if(!data) return 0;

	int fd = 0; u64 read_e = 0;

	if(offset < 0) offset = 0; else memset(data, 0, size);

#ifdef USE_NTFS
	if(is_ntfs_path(file))
	{
		fd = ps3ntfs_open(file + 5, O_RDONLY, 0);
		if(fd >= 0)
		{
			ps3ntfs_seek64(fd, offset, SEEK_SET);
			read_e = ps3ntfs_read(fd, (void *)data, size);
			ps3ntfs_close(fd);
		}
		return read_e;
	}
#endif

	if(cellFsOpen(file, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		if(cellFsReadWithOffset(fd, offset, (void *)data, size, &read_e) != CELL_FS_SUCCEEDED) read_e = 0;
		cellFsClose(fd);
	}

	return read_e;
}

static u16 read_sfo(const char *file, char *data)
{
	return (u16)read_file(file, data, _4KB_, 0);
}

static int write_file(const char *file, int flags, const char *data, u64 offset, int size, bool crlf)
{
	int fd = 0;

#ifdef USE_NTFS
	if(is_ntfs_path(file))
	{
		int nflags = O_CREAT | O_WRONLY;
		if(flags & CELL_FS_O_APPEND) nflags |= O_APPEND;
		if(flags & CELL_FS_O_TRUNC)  nflags |= O_TRUNC;

		fd = ps3ntfs_open(file + 5, nflags, MODE);
		if(fd >= 0)
		{
			if(offset) ps3ntfs_seek64(fd, offset, SEEK_SET);
			if((size <= SAVE_ALL) && data) size = strlen(data);
			if( size ) ps3ntfs_write(fd, data, size);
			if( crlf ) ps3ntfs_write(fd, (void *)"\r\n", 2);
			ps3ntfs_close(fd);
			return CELL_FS_SUCCEEDED;
		}
		return FAILED;
	}
#endif

	cellFsChmod(file, MODE); // set permissions for overwrite

	if(cellFsOpen(file, flags, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		if(offset) cellFsLseek(fd, offset, CELL_FS_SEEK_SET, &offset);
		if((size <= SAVE_ALL) && data) size = strlen(data);
		if( size ) cellFsWrite(fd, (void *)data, size, NULL);
		if( crlf ) cellFsWrite(fd, (void *)"\r\n", 2, NULL);
		cellFsClose(fd);

		cellFsChmod(file, MODE); // set permissions if created

		return CELL_FS_SUCCEEDED;
	}

	return FAILED;
}

int save_file(const char *file, const char *mem, s64 size)
{
	bool crlf = (size == APPEND_TEXT); // auto add new line

	int flags = CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY;
	if( size < 0 )  {flags = CELL_FS_O_APPEND | CELL_FS_O_CREAT | CELL_FS_O_WRONLY; size = crlf ? SAVE_ALL : -size;} else
	if(!extcmp(file, "/PARAM.SFO", 10)) flags = CELL_FS_O_CREAT | CELL_FS_O_WRONLY;
/*
	cellFsChmod(file, MODE); // set permissions for overwrite

	int fd = 0;
	if(cellFsOpen(file, flags, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		if(size) cellFsWrite(fd, (void *)mem, size, NULL);
		if(crlf) cellFsWrite(fd, (void *)"\r\n", 2, NULL);
		cellFsClose(fd);

		cellFsChmod(file, MODE); // set permissions if created

		return CELL_FS_SUCCEEDED;
	}
	return FAILED;
*/
	return write_file(file, flags, mem, 0, (int)size, crlf);
}
/*
static void addlog(const char *msg1, const char *msg2, u64 i)
{
	char msg[200];
	snprintf(msg, 199, "%llx %s %s", i, msg1, msg2);
	save_file("/dev_hdd0/wmm.log", msg, APPEND_TEXT);
}
*/
static int sysLv2FsBdDecrypt(void)
{
	system_call_1(SC_BDVD_DECRYPT, (u64) "/dev_bdvd");
	return_to_user_prog(int);
}

static int sysLv2FsLink(const char *oldpath, const char *newpath)
{
	system_call_2(SC_FS_LINK, (u64)(u32)oldpath, (u64)(u32)newpath);
	return_to_user_prog(int);
}

/*
static int file_concat(const char *file1, char *file2)
{
	struct CellFsStat buf;
	int fd1, fd2;
	int ret = FAILED;

	filepath_check(file2);

	if(islike(file1, "/dvd_bdvd"))
		sysLv2FsBdDecrypt(); // decrypt dev_bdvd files

	if(cellFsStat(file1, &buf) != CELL_FS_SUCCEEDED) return ret;

	if(cellFsOpen(file1, CELL_FS_O_RDONLY, &fd1, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		u64 size = buf.st_size;

		sys_addr_t sysmem = 0; u64 chunk_size = _64KB_;

		if(sys_memory_allocate(chunk_size, SYS_MEMORY_PAGE_SIZE_64K, &sysmem) == CELL_OK)
		{
			// append
			if(cellFsOpen(file2, CELL_FS_O_CREAT | CELL_FS_O_RDWR | CELL_FS_O_APPEND, &fd2, 0, 0) == CELL_FS_SUCCEEDED)
			{
				char *chunk = (char*)sysmem;
				u64 read = 0, written = 0, pos=0;
				copy_aborted = false;

				while(size > 0)
				{
					if(copy_aborted) break;

					cellFsLseek(fd1, pos, CELL_FS_SEEK_SET, &read);
					cellFsRead(fd1, chunk, chunk_size, &read);

					cellFsWrite(fd2, chunk, read, &written);
					if(!written) {break;}

					pos+=written;
					size-=written;
					if(chunk_size>size) chunk_size=(int) size;
				}
				cellFsClose(fd2);

				if(copy_aborted)
					cellFsUnlink(file2); //remove incomplete file
				else
					{cellFsChmod(file2, MODE); copied_count++;}

				ret=size;
			}
			sys_memory_free(sysmem);
		}
		cellFsClose(fd1);
	}

	return ret;
}
*/

//////////////////////////////////////////////////////////////

int64_t file_copy(char *file1, char *file2, u64 maxbytes)
{
	struct CellFsStat buf;
	int fd1, fd2;
	int64_t ret = FAILED;
	copy_aborted = false;

	filepath_check(file2);

	if(IS(file1, file2)) return FAILED;

#ifdef USE_NTFS
	bool is_ntfs1 = is_ntfs_path(file1), is_ntfs2 = false;
#else
	bool is_ntfs1 = false, is_ntfs2 = false;
#endif

#ifdef COPY_PS3
	if(!ftp_state) sprintf(current_file, "%s", file2);
#endif

#ifdef USE_NTFS
	if(is_ntfs1)
	{
		struct stat bufn;
		if(ps3ntfs_stat(file1 + 5, &bufn) >= 0) buf.st_size = bufn.st_size; else return FAILED;
	}
	else
#endif
	if(cellFsStat(file1, &buf) != CELL_FS_SUCCEEDED)
	{
#ifndef LITE_EDITION
#ifdef COBRA_ONLY
		if(islike(file1, "/net"))
		{
			int ns = connect_to_remote_server((file1[4] & 0x0F));
			copy_net_file(file2, (char*)file1 + 5, ns, maxbytes);
			if(ns >= 0) sclose(&ns);

			if(file_exists(file2)) return 0;
		}
#endif
#endif
		return FAILED;
	}

	u16 flen1 = 0;
	bool check_666 = false;

#ifdef UNLOCK_SAVEDATA
	if(webman_config->unlock_savedata && (buf.st_size < _4KB_))
	{
		u16 size = (u16)buf.st_size;
		unsigned char data[_4KB_]; *data = NULL;
		if(unlock_param_sfo(file1, data, size))
		{
			save_file(file2, (void*)data, size);
			return size;
		}
	}
#endif

	if(islike(file2, INT_HDD_ROOT_PATH))
	{
		if(islike(file1, INT_HDD_ROOT_PATH))
		{
			cellFsUnlink(file2); copied_count++;
			return sysLv2FsLink(file1, file2);
		}
		else
			check_666 = islike(file1, "/dev_usb");

		if(check_666)
		{
			flen1 = strlen(file1) - 6;
			check_666 = islike(file1 + flen1, ".666");
			if(check_666)
			{
				if(!islike(file1 + flen1, ".66600")) return 0; // ignore .666xx
				u16 flen2 = strlen(file2) - 6;
				if(islike(file2 + flen2, ".66600")) file2[flen2] = NULL; // remove .66600
			}
		}

		if(buf.st_size > get_free_space("/dev_hdd0")) return FAILED;
	}

	if(allow_sc36 && islike(file1, "/dev_bdvd"))
		sysLv2FsBdDecrypt(); // decrypt dev_bdvd files

#ifdef USE_NTFS
	if(is_ntfs1)
	{
		fd1 = ps3ntfs_open(file1 + 5, O_RDONLY, 0);
		if(fd1 < 0) is_ntfs1 = false;
	}
	else
#endif
	// skip if file already exists with same size
	if(dont_copy_same_size)
	{
		if(file_ssize(file2) == (s64)buf.st_size)
		{
			copied_count++;
			return buf.st_size;
		}
	}

	u64 pos;
	u8 merge_part = 0;

merge_next:

	if(is_ntfs1 || cellFsOpen(file1, CELL_FS_O_RDONLY, &fd1, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		sys_addr_t sysmem = NULL; u64 chunk_size = (buf.st_size <= _64KB_) ? _64KB_ : _256KB_;

		if(g_sysmem) sysmem = g_sysmem; else
		{
			sys_memory_container_t vsh_mc = get_vsh_memory_container();
			if(vsh_mc)	sys_memory_allocate_from_container(chunk_size, vsh_mc, SYS_MEMORY_PAGE_SIZE_64K, &sysmem);
		}

		if(!sysmem) chunk_size = _64KB_;

		if(sysmem || (!sysmem && sys_memory_allocate(chunk_size, SYS_MEMORY_PAGE_SIZE_64K, &sysmem) == CELL_OK))
		{
			u64 size = buf.st_size, part_size = buf.st_size; u8 part = 0;
			if(maxbytes > 0 && size > maxbytes) size = maxbytes;

			if((part_size > 0xFFFFFFFFULL) && islike(file2, "/dev_usb"))
			{
				if(is_ext(file2, ".iso")) strcat(file2, ".0"); else strcat(file2, ".66600");
				part++; part_size = 0xFFFF0000ULL; //4Gb - 64kb
			}

			u64 read = 0, written = 0;
			char *chunk = (char*)sysmem;
			u16 flen = strlen(file2);
next_part:

#ifdef USE_NTFS
			is_ntfs2 = is_ntfs_path(file2);
			if(is_ntfs2)
			{
				fd2 = ps3ntfs_open(file2 + 5, O_CREAT | O_WRONLY | O_TRUNC, MODE);
				if(fd2 < 0) is_ntfs2 = false;
			}
#endif
			// copy_file
			if(is_ntfs2 || merge_part || cellFsOpen(file2, CELL_FS_O_CREAT | CELL_FS_O_WRONLY | CELL_FS_O_TRUNC, &fd2, 0, 0) == CELL_FS_SUCCEEDED)
			{
				pos = 0;
				while(size > 0)
				{
					if(copy_aborted) break;

#ifdef USE_NTFS
					if(is_ntfs1)
					{
						ps3ntfs_seek64(fd1, pos, SEEK_SET);
						read = ps3ntfs_read(fd1, (void *)chunk, chunk_size);
					}
					else
#endif
					{
						cellFsReadWithOffset(fd1, pos, chunk, chunk_size, &read);
					}

					if(!read) break;

#ifdef USE_NTFS
					if(is_ntfs2)
					{
						written = ps3ntfs_write(fd2, chunk, read);
					}
					else
#endif
					cellFsWrite(fd2, chunk, read, &written);

					if(!written) break;

					pos  += written;
					size -= written;

					if(chunk_size > size) chunk_size = (int) size;

					part_size -= written;
					if(part_size == 0) break;

					sys_ppu_thread_usleep(1000);
				}

				if(check_666 && !copy_aborted && (merge_part < 99))
				{
					sprintf(file1 + flen1, ".666%02i", ++merge_part);

					if(file_exists(file1))
					{
						cellFsClose(fd1);
						goto merge_next;
					}
				}

#ifdef USE_NTFS
				if(is_ntfs2) ps3ntfs_close(fd2);
				else
#endif
				cellFsClose(fd2);

				if(copy_aborted)
				{
#ifdef USE_NTFS
					if(is_ntfs2) ps3ntfs_unlink(file2 + 5);
					else
#endif
					cellFsUnlink(file2); //remove incomplete file
				}
				else if((part > 0) && (size > 0))
				{
					if(file2[flen - 2] == '.')
						sprintf(file2 + flen - 2, ".%i", part);
					else
						sprintf(file2 + flen - 2, "%02i", part);

					part++; part_size = 0xFFFF0000ULL;
					goto next_part;
				}
				else
					{cellFsChmod(file2, MODE); copied_count++;}

				ret = size;
			}

			if(!g_sysmem) sys_memory_free(sysmem);
		}

#ifdef USE_NTFS
		if(is_ntfs1) ps3ntfs_close(fd1);
		else
#endif
		cellFsClose(fd1);
	}

	return ret;
}

static void _file_copy(char *file1, char *file2)
{
	if(not_exists(file1)) return;
	dont_copy_same_size = false; // force copy file with the same size than existing file
	file_copy(file1, file2, COPY_WHOLE_FILE);
	dont_copy_same_size = true;  // restore default mode (assume file is already copied if existing file has same size)
}

#ifdef COPY_PS3
static int folder_copy(const char *path1, char *path2)
{
	filepath_check(path2);

	int fd; bool is_ntfs = false;

	copy_aborted = false;

#ifdef USE_NTFS
	struct stat bufn;
	DIR_ITER *pdir = NULL;

	if(is_ntfs_path(path1))
	{
		pdir = ps3ntfs_opendir((char*)path1);
		if(pdir) is_ntfs = true;
	}
	else
#endif
	{
		cellFsChmod(path1, DMODE);
	}

	bool is_root = IS(path1, "/");

	if(is_ntfs || cellFsOpendir(path1, &fd) == CELL_FS_SUCCEEDED)
	{
		if(islike(path1, "/dev_bdvd"))
			{allow_sc36 = false; sysLv2FsBdDecrypt();} // decrypt dev_bdvd files

#ifdef USE_NTFS
		if(is_ntfs_path(path2))
			ps3ntfs_mkdir(path2 + 5, DMODE);
		else
#endif
			cellFsMkdir(path2, DMODE);

		CellFsDirent dir; u64 read_e;
		CellFsDirectoryEntry entry; u32 read_f;
		char *entry_name = (is_root) ? dir.d_name : entry.entry_name.d_name;

		char source[STD_PATH_LEN];
		char target[STD_PATH_LEN];

		if(!g_sysmem)
		{
			sys_memory_container_t vsh_mc = get_vsh_memory_container();
			if(vsh_mc)	sys_memory_allocate_from_container(_256KB_, vsh_mc, SYS_MEMORY_PAGE_SIZE_64K, &g_sysmem);
		}

		u16 plen1 = sprintf(source, "%s", path1);
		u16 plen2 = sprintf(target, "%s", path2);

		while(working)
		{
#ifdef USE_NTFS
			if(is_ntfs)
			{
				if(ps3ntfs_dirnext(pdir, entry_name, &bufn)) break;
				if(entry_name[0]=='$' && path1[12] == 0) continue;
			}
			else
#endif
			if(is_root && ((cellFsReaddir(fd, &dir, &read_e) != CELL_FS_SUCCEEDED) || (read_e == 0))) break;
			else
			if(cellFsGetDirectoryEntries(fd, &entry, sizeof(entry), &read_f) || !read_f) break;

			if(copy_aborted) break;
			if(entry_name[0] == '.' && (entry_name[1] == '.' || entry_name[1] == NULL)) continue;

			sprintf(source + plen1, "/%s", entry_name);
			sprintf(target + plen2, "/%s", entry_name);

			if(isDir(source))
			{
				if(IS(source, "/dev_bdvd/PS3_UPDATE")) {cellFsMkdir(target, DMODE); continue;} // just create /PS3_UPDATE without its content
				folder_copy(source, target);
			}
			else
				file_copy(source, target, COPY_WHOLE_FILE);
		}

		if(g_sysmem) {sys_memory_free(g_sysmem); g_sysmem = NULL;}

#ifdef USE_NTFS
		if(is_ntfs) ps3ntfs_dirclose(pdir);
		else
#endif
		cellFsClosedir(fd); allow_sc36 = true;

		if(copy_aborted) return FAILED;
	}
	else
		return FAILED;

	return CELL_FS_SUCCEEDED;
}

static u8  do_chmod = 0;
static u16 new_mode = 0777;
static u8  check_md5 = 0;
static u32 dir_count = 0;
static u32 file_count = 0;
static char *stitle_id = NULL;

#ifdef CALC_MD5
static void calc_md5(const char *filename, char *md5);
#endif
static size_t size_link(const char *source)
{
	size_t size = file_size(source);

	if(!stitle_id) {file_count++; return size;}

	if(islike(source, INT_HDD_ROOT_PATH))
	{
		save_file(FILE_LIST_TXT, current_file, APPEND_TEXT);

		char *usrdir = strstr(source, "/USRDIR/");
		if(usrdir)
		{
			if(!islike(usrdir + 8, "EBOOT.BIN"))
			{
				char game_path[MAX_PATH_LEN];
				sprintf(game_path, "%s%s%s", _HDD0_GAME_DIR, stitle_id, usrdir);
				if(file_size(game_path) == size)
				{
					if(copy_aborted) return 0;
					strcpy(current_file, source);

					#ifdef CALC_MD5
					if(check_md5)
					{
						char *hash1 = cp_path;
						char *hash2 = cp_path + 40;
						calc_md5(source, hash1);
						if(copy_aborted) return 0;
						calc_md5(game_path, hash2);
						if(!IS(hash1, hash2)) return 0;
					}
					#endif
					if(copy_aborted) return 0;

					cellFsUnlink(game_path);
					sysLv2FsLink(source, game_path);

					strncpy(game_path, "lnk->", 5);

					save_file(FILE_LIST_TXT, game_path, APPEND_TEXT);
					file_count++; return size;
				}
			}
		}
	}
	return 0;
}

static u64 folder_size(const char *path)
{
	if(!isDir(path))
	{
		if(do_chmod) cellFsChmod(path, new_mode);

		return file_size(path);
	}

	int fd; bool is_ntfs = false;

	u64 dir_size = 0;

	copy_aborted = false;

#ifdef USE_NTFS
	struct stat bufn;
	DIR_ITER *pdir = NULL;

	if(is_ntfs_path(path))
	{
		pdir = ps3ntfs_opendir((char*)path);
		if(pdir) is_ntfs = true;
	}
#endif

	if(is_ntfs || cellFsOpendir(path, &fd) == CELL_FS_SUCCEEDED)
	{
		CellFsDirectoryEntry entry; u32 read_f;
		char *entry_name = entry.entry_name.d_name;

		char source[STD_PATH_LEN];
		u16 plen1 = sprintf(source, "%s", path);
		char *psource = source + plen1;

		while(working)
		{
#ifdef USE_NTFS
			if(is_ntfs)
			{
				if(ps3ntfs_dirnext(pdir, entry_name, &bufn)) break;
			}
			else
#endif
			if(cellFsGetDirectoryEntries(fd, &entry, sizeof(entry), &read_f) || !read_f) break;

			if(copy_aborted) break;
			if(entry_name[0] == '.' && (entry_name[1] == '.' || entry_name[1] == NULL)) continue;

			sprintf(psource, "/%s", entry_name); if(do_chmod) cellFsChmod(source, new_mode);

			if(isDir(source))
			{
				dir_count++;
				dir_size += folder_size(source);
			}
			else
			{
				dir_size += size_link(source);
			}
		}

#ifdef USE_NTFS
		if(is_ntfs) ps3ntfs_dirclose(pdir);
		else
#endif
		cellFsClosedir(fd);
	}

	return dir_size;
}
#endif // #ifdef COPY_PS3

//////////////////////////////////////////////////////////////

#ifdef COPY_PS3
static int scan(const char *path, u8 recursive, const char *wildcard, enum scan_operations fop, char *dest)
{
	// fop: 0 = scan to file, 1 = del, 2 = copy, 3 = move, 4 = rename/move in same fs

	if(recursive == RECURSIVE_DELETE) ; else
	if(!sys_admin || !working) return FAILED;


#ifdef USE_NTFS
	if((fop == SCAN_DELETE) && !wildcard && !isDir(path))
	{
		if(is_ntfs_path(path))
			return ps3ntfs_unlink(path + 5);
		else
			return cellFsUnlink(path);
	}
#else
	if((fop == SCAN_DELETE) && !wildcard && !isDir(path)) return cellFsUnlink(path);
#endif

	if((fop == SCAN_DELETE || fop == SCAN_TRUNCATE) && (strlen(path) < 11 || islike(path, "/dev_bdvd") || islike(path, "/dev_flash") || islike(path, "/dev_blind"))) return FAILED;

	size_t wildcard_len = (wildcard) ? strlen(wildcard) : 0;
	char *wildcard1 = NULL, *wildcard2 = NULL, wcard[wildcard_len + 1];
	char *(*instr)(const char *, const char *) = &strstr; bool wfound1 = true, wfound2 = true;
	if(wildcard_len)
	{
		sprintf(wcard, "%s", wildcard);

		wildcard1 = wcard;
		if(*wildcard1 == '~') {wildcard1++, wfound1 = false;}							// *~TEXT = exclude files
		if(*wildcard1 == '^') {wildcard1++, instr = &strcasestr;}						// *^TEXT = case insensitive search
		if( wfound1 && (*wildcard1 == '~')) {wildcard1++, wfound1 = false;}				// <-- accept prefixes: ~^ or ^~

		wildcard2 = strstr((char*)wildcard1, "*"); if(wildcard2) *wildcard2++ = NULL;	// *TEXT1*TEXT2 = text1 and text2
		if(wildcard2 && (*wildcard2 == '~')) {wildcard2++, wfound2 = false;}
	}

	int fd; bool is_ntfs = false;

	copy_aborted = false;

#ifdef USE_NTFS
	struct stat bufn;
	DIR_ITER *pdir;

	if(is_ntfs_path(path))
	{
		pdir = ps3ntfs_opendir((char*)path);
		if(pdir) is_ntfs = true;
	}
#endif

	bool is_root = IS(path, "/");

	if(is_ntfs || cellFsOpendir(path, &fd) == CELL_FS_SUCCEEDED)
	{
		CellFsDirent dir; u64 read_e;
		CellFsDirectoryEntry entry_d; u32 read_f;
		char *entry_name = (is_root) ? dir.d_name : entry_d.entry_name.d_name;

		char entry[STD_PATH_LEN], dest_entry[STD_PATH_LEN];

		u16 path_len = sprintf(entry, "%s", path);
		bool p_slash = (path_len > 1) && (path[path_len - 1] == '/');
		char *pentry = entry + path_len;

		bool use_dest = ((fop >= 2) && (fop <= 5)); // fop: 2 = copy, 3 = move, 4 = rename/move in same fs, 5 = copy bk

		if(use_dest) {mkdir_tree(dest); if(!isDir(dest)) return FAILED;}

		u16 dest_len = sprintf(dest_entry, "%s", dest);
		char *pdest = dest_entry + dest_len;

		while(working)
		{
#ifdef USE_NTFS
			if(is_ntfs)
			{
				if(ps3ntfs_dirnext(pdir, entry_name, &bufn)) break;
				if(entry_name[0]=='$' && path[12] == 0) continue;
			}
			else
#endif
			if(is_root && ((cellFsReaddir(fd, &dir, &read_e) != CELL_FS_SUCCEEDED) || (read_e == 0))) break;
			else
			if(cellFsGetDirectoryEntries(fd, &entry_d, sizeof(entry_d), &read_f) || !read_f) break;

			if(copy_aborted) break;
			if(entry_name[0] == '.' && (entry_name[1] == '.' || entry_name[1] == NULL)) continue;

			if(p_slash) sprintf(pentry, "%s", entry_name); else sprintf(pentry, "/%s", entry_name);

			if(use_dest) sprintf(pdest, "/%s", entry_name);

			if(isDir(entry))
				{if(recursive) scan(entry, recursive, wildcard, fop, dest);}

			else if(wildcard1 && (*wildcard1!=NULL) && ((!instr(pentry, wildcard1)) == wfound1)) continue;
			else if(wildcard2 && (*wildcard2!=NULL) && ((!instr(pentry, wildcard2)) == wfound2)) continue;

			else if(fop == SCAN_LIST || fop == SCAN_LIST_SIZE)
			{
				if(!dest || *dest != '/') break;

				// add size column if fop == SCAN_LIST_SIZE
				if(fop) {sprintf(entry_name, "\t%'llu", file_size(entry)); strcat(entry, entry_name);}

				save_file(dest, entry, APPEND_TEXT);
			}
#ifdef UNLOCK_SAVEDATA
			else if(fop == SCAN_UNLOCK_SAVE)
			{
				char sfo[_4KB_];
				u16 sfo_size = read_sfo(entry, sfo);
				if(unlock_param_sfo(entry, (unsigned char *)sfo, sfo_size))
				{
					save_file(entry, (void*)sfo, sfo_size);
				}
			}
#endif
			else if(fop == SCAN_TRUNCATE)
			{
				save_file(entry, "", SAVE_ALL);
			}
			else if(fop == SCAN_COPY || fop == SCAN_COPYBK)
			{
				file_copy(entry, dest_entry, COPY_WHOLE_FILE); // copy ntfs & cellFS

				if((fop == SCAN_COPYBK) && file_exists(dest_entry))
					{sprintf(dest_entry, "%s.bak", entry); cellFsRename(entry, dest_entry);}
			}
#ifdef USE_NTFS
			else if(is_ntfs)
			{
				if(fop == SCAN_DELETE) {ps3ntfs_unlink(entry + 5);} else
			//	if(fop == SCAN_COPY  ) {ps3ntfs_copy(entry, dest_entry); else
				if(fop == SCAN_MOVE  ) {if(file_copy(entry, dest_entry, COPY_WHOLE_FILE) >= CELL_OK) ps3ntfs_unlink(entry + 5);} else
				if(fop == SCAN_RENAME) {ps3ntfs_rename(entry + 5, dest_entry);}
			}
#endif
			else
			{
				if(fop == SCAN_DELETE) {cellFsUnlink(entry);} else
			//	if(fop == SCAN_COPY  ) {file_copy(entry, dest_entry);} else
				if(fop == SCAN_MOVE  ) {if(file_copy(entry, dest_entry, COPY_WHOLE_FILE) >= CELL_OK) cellFsUnlink(entry);} else
				if(fop == SCAN_RENAME) {cellFsRename(entry, dest_entry);}
			}
		}

#ifdef USE_NTFS
		if(is_ntfs) ps3ntfs_dirclose(pdir);
		else
#endif
		cellFsClosedir(fd);

		if(copy_aborted) return FAILED;
	}
	else
		return FAILED;

	if((recursive > 0) && (fop == SCAN_DELETE))
	{
#ifdef USE_NTFS
		if(is_ntfs)
			ps3ntfs_unlink(path + 5);
		else
#endif
			cellFsRmdir(path);
	}

	return CELL_FS_SUCCEEDED;
}

static int del(const char *path, u8 recursive)
{
	return scan(path, recursive, NULL, SCAN_DELETE, NULL);
}
/*
static int del(const char *path, u8 recursive)
{
	if(recursive == RECURSIVE_DELETE) ; else
	if(!sys_admin || !working) return FAILED;

#ifdef USE_NTFS
	if(!isDir(path))
	{
		if(is_ntfs_path(path))
			return ps3ntfs_unlink(path + 5);
		else
			return cellFsUnlink(path);
	}
#else
	if(!isDir(path)) return cellFsUnlink(path);
#endif

	if(strlen(path) < 11 || islike(path, "/dev_bdvd") || islike(path, "/dev_flash") || islike(path, "/dev_blind")) return FAILED;

	int fd; bool is_ntfs = false;

	copy_aborted = false;

#ifdef USE_NTFS
	struct stat bufn;
	DIR_ITER *pdir;

	if(is_ntfs_path(path))
	{
		pdir = ps3ntfs_opendir((char*)path);
		if(pdir) is_ntfs = true;
	}
#endif

	if(is_ntfs || cellFsOpendir(path, &fd) == CELL_FS_SUCCEEDED)
	{
		CellFsDirent dir; u64 read_e;

		char entry[STD_PATH_LEN];

		while(working)
		{
#ifdef USE_NTFS
			if(is_ntfs)
			{
				if(ps3ntfs_dirnext(pdir, dir.d_name, &bufn)) break;
				if(dir.d_name[0]=='$' && path[12] == 0) continue;
			}
			else
#endif
			if((cellFsReaddir(fd, &dir, &read_e) != CELL_FS_SUCCEEDED) || (read_e == 0)) break;

			if(copy_aborted) break;
			if(dir.d_name[0] == '.' && (dir.d_name[1] == '.' || dir.d_name[1] == NULL)) continue;

			sprintf(entry, "%s/%s", path, dir.d_name);

			if(isDir(entry))
				{if(recursive) del(entry, recursive);}
#ifdef USE_NTFS
			else if(is_ntfs)
				ps3ntfs_unlink(entry + 5);
#endif
			else
				cellFsUnlink(entry);
		}

#ifdef USE_NTFS
		if(is_ntfs) ps3ntfs_dirclose(pdir);
		else
#endif
		cellFsClosedir(fd);

		if(copy_aborted) return FAILED;
	}
	else
		return FAILED;

	if(recursive)
	{
#ifdef USE_NTFS
		if(is_ntfs) ps3ntfs_unlink(path + 5);
		else
#endif
		cellFsRmdir(path);
	}

	return CELL_FS_SUCCEEDED;
}*/
#endif

static void unlink_file(const char *drive, const char *path, const char *file)
{
	char filename[64];
	sprintf(filename, "%s/%s%s", drive, path, file); cellFsUnlink(filename);
}

static void delete_history(bool delete_folders)
{
	int fd; char path[64];

	if(cellFsOpendir(HDD0_HOME_DIR, &fd) == CELL_FS_SUCCEEDED)
	{
		CellFsDirectoryEntry dir; u32 read_e;
		char *entry_name = dir.entry_name.d_name;

		while(working && (!cellFsGetDirectoryEntries(fd, &dir, sizeof(dir), &read_e) && read_e))
		{
			unlink_file(HDD0_HOME_DIR, entry_name, "/etc/boot_history.dat");
			unlink_file(HDD0_HOME_DIR, entry_name, "/etc/community/CI.TMP");
			unlink_file(HDD0_HOME_DIR, entry_name, "/community/MI.TMP");
			unlink_file(HDD0_HOME_DIR, entry_name, "/community/PTL.TMP");
		}
		cellFsClosedir(fd);
	}

	unlink_file("/dev_hdd0", "vsh/pushlist/", "game.dat");
	unlink_file("/dev_hdd0", "vsh/pushlist/", "patch.dat");

	if(!delete_folders || !working) return;

	for(u8 p = 0; p < 10; p++)
	{
		sprintf(path, "%s/%s", drives[0], paths[p]); cellFsRmdir(path);
		strcat(path, AUTOPLAY_TAG); 				 cellFsRmdir(path);
	}
	cellFsRmdir("/dev_hdd0/PKG");
}

static void del_turnoff(u8 beeps)
{
	do_umount(false);
	cellFsUnlink((char*)"/dev_hdd0/tmp/turnoff");

#ifdef WM_REQUEST
	cellFsUnlink(WMREQUEST_FILE);
#endif
#ifdef WEB_CHAT
	cellFsUnlink(WMCHATFILE);
#endif

	if(!webman_config->nobeep)
	{
		if(beeps == 1) { BEEP1 }
		if(beeps == 2) { BEEP2 }
	}
}

//////////////////////////////////////////////////////////////

static int wait_path(const char *path, u8 timeout, bool found)
{
	if(*path!='/') return FAILED;

	for(u8 n = 0; n < (timeout * 20); n++)
	{
		if(file_exists(path) == found) return CELL_FS_SUCCEEDED;
		if(!working) break;
		sys_ppu_thread_usleep(50000);
	}
	return FAILED;
}

int wait_for(const char *path, u8 timeout)
{
	return wait_path(path, timeout, true);
}

#define MAX_WAIT	30

static u8 wait_for_xmb(void)
{
	u8 t = 0;
	while(View_Find("explore_plugin") == 0) {if(++t > MAX_WAIT) break; sys_ppu_thread_sleep(1);}
	return (t > MAX_WAIT); // true = timeout
}

//////////////////////////////////////////////////////////////

/*
static u64 syscall_837(const char *device, const char *format, const char *point, u32 a, u32 b, u32 c, void *buffer, u32 len)
{
	system_call_8(SC_FS_MOUNT, (u64)device, (u64)format, (u64)point, a, b, c, (u64)buffer, len);
	return_to_user_prog(u64);
}
*/

static void mount_device(const char *dev_path, const char *dev_name, const char *file_system)
{
	if(!sys_admin) return;

	if(!dev_path || (*dev_path != '/') || isDir(dev_path)) return;

	if(islike(dev_path, "/dev_blind"))
		{system_call_8(SC_FS_MOUNT, (u64)(char*)"CELL_FS_IOS:BUILTIN_FLSH1", (u64)(char*)"CELL_FS_FAT", (u64)(char*)"/dev_blind", 0, 0, 0, 0, 0);}
	else if(!dev_name || !file_system || islike(dev_path, "/dev_hdd1"))
		{system_call_8(SC_FS_MOUNT, (u64)(char*)"CELL_FS_UTILITY:HDD1", (u64)(char*)"CELL_FS_FAT", (u32)dev_path, 0, 0, 0, 0, 0);}
	else if(islike(dev_name, "CELL_FS_") && islike(file_system, "CELL_FS_"))
		{system_call_8(SC_FS_MOUNT, (u32)dev_name, (u32)file_system, (u32)dev_path, 0, 0, 0, 0, 0);}
}

static void enable_dev_blind(const char *msg)
{
	if(!sys_admin) return;

	mount_device("/dev_blind", NULL, NULL);

	if(!msg) return;

	show_msg(msg);
	sys_ppu_thread_sleep(2);
}

static void disable_dev_blind(void)
{
	system_call_3(SC_FS_UMOUNT, (u64)(char*)"/dev_blind", 0, 1);
}

#if defined(WM_CUSTOM_COMBO) || defined(WM_REQUEST)
static void handle_file_request(const char *wm_url)
{
	if(wm_url || file_exists(WMREQUEST_FILE))
	{
		do_web_command(WM_FILE_REQUEST, wm_url);
	}
}

static bool do_custom_combo(const char *filename)
{
 #if defined(WM_CUSTOM_COMBO)
	char combo_file[128];

	if(*filename == '/')
		snprintf(combo_file, 127, "%s", filename);
	else
		snprintf(combo_file, 127, "%s%s", WM_CUSTOM_COMBO, filename); // use default path
 #else
	const char *combo_file = filename;
 #endif

	if(file_exists(combo_file))
	{
		char *url = html_base_path;
		read_file(combo_file, url, HTML_RECV_LAST, 0);
		handle_file_request(url); *html_base_path = NULL;
		return true;
	}
	return false;
}
#endif

#ifdef VISUALIZERS

#define MAP_SELECTED	0x00
#define DEFAULT_RES		0xFF

static u8 map_vsh_resource(u8 res_id, u8 id, char *param, u8 set)
{
	const char *hdd_path =  (res_id == 0) ? "/dev_hdd0/tmp/wallpaper": // 0
							(res_id == 1) ? "/dev_hdd0/tmp/earth"    : // 1
							(res_id == 2) ? "/dev_hdd0/tmp/canyon"   : // 2
							(res_id == 3) ? "/dev_hdd0/tmp/lines"    : // 3
							(res_id == 4) ? "/dev_hdd0/tmp/coldboot" : // 4
						//	(res_id == 7) ? "/dev_hdd0/tmp/gameboot" : // 7
											"/dev_hdd0/tmp/theme";     // 5 & 6 (last selected theme)

	const char *res_path =  (res_id == 1) ? "/dev_flash/vsh/resource/qgl/earth.qrc" :
							(res_id == 2) ? "/dev_flash/vsh/resource/qgl/canyon.qrc":
							(res_id == 3) ? "/dev_flash/vsh/resource/qgl/lines.qrc" :
											"/dev_flash/vsh/resource/coldboot_stereo.ac3";

	if(isDir(hdd_path))
	{
		if(!set && !id)	// MAP_SELECTED
		{
			id = webman_config->resource_id[res_id];
		}

		u8 _id, save = false;
		if(!id)	// random
		{
			CellRtcTick nTick; cellRtcGetCurrentTick(&nTick);
			_id = nTick.tick % 0x100;
		}
		else
			_id = id;	// fixed

		u8 loop = 0;
		do
		{
			if(loop) _id /= 2;

			if(res_id == 0)
				sprintf(param, "%s/%i.png", hdd_path, _id); // wallpaper
			else if(res_id == 4)
				sprintf(param, "%s/%i.ac3", hdd_path, _id); // coldboot
			else if(res_id == 5)
				sprintf(param, "%s/%i.p3t", hdd_path, _id); // theme
			//else if(res_id == 7)
			//	sprintf(param, "%s/%i/custom_render_plugin.rco", hdd_path, _id); // gameboot
			else
				sprintf(param, "%s/%i.qrc", hdd_path, _id); // lines, earth, canyon

			if(id == DEFAULT_RES) break; loop = 1;
		}
		while(_id && not_exists(param));

		if(file_exists(param))
		{
			if(res_id == 5)
			{
				if((set || (_id > 0)) && (webman_config->resource_id[6] != _id))
				{
					char msg[0x100];
					scan("/dev_hdd0/theme/", false, "CD_*.p3t", SCAN_DELETE, msg); // delete temporary themes
					wait_for_xmb();
					installPKG(param, msg);
					set = save = true, webman_config->resource_id[6] = _id; // last selected theme
				}
			}
			else if(res_id)
			{
				sys_map_path(res_path, param);
				if(res_id == 4)
					sys_map_path("/dev_flash/vsh/resource/coldboot_multi.ac3",  param);
				/*else if(res_id == 7)
				{
					sprintf(param, "%s/%i/gameboot_multi.ac3", hdd_path, _id);
					sys_map_path("/dev_flash/vsh/resource/gameboot_multi.ac3", param);
					sprintf(param, "%s/%i/gameboot_stereo.ac3", hdd_path, _id);
					sys_map_path("/dev_flash/vsh/resource/gameboot_stereo.ac3", param);
					sprintf(param, "%s/%i/custom_render_plugin.rco", hdd_path, _id);
					sys_map_path("/dev_flash/vsh/resource/custom_render_plugin.rco", param);
				}*/
			}
			else
			{
				char bg[48];
				sprintf(bg, "%s/%08i/theme/wallpaper.png", HDD0_HOME_DIR, xusers()->GetCurrentUserNumber());
				cellFsUnlink(bg);
				return sysLv2FsLink(param, bg);
			}
		}
		else if(res_id)
		{
			if(id != DEFAULT_RES) id = 0;
			if(res_id == 5)
				webman_config->resource_id[6] = 0; // reset last selected theme
			else
				strcpy(param, res_path);
			sys_map_path(param, NULL);
		}

		if(set && (save || (webman_config->resource_id[res_id] != id)))
		{
			webman_config->resource_id[res_id] = id;

			save_settings(); // save if setting changed
		}
	}
	return id;
}

static void randomize_vsh_resources(bool apply_theme, char *param)
{
	map_vsh_resource(0, MAP_SELECTED, param, false); // wallpaper.png
	map_vsh_resource(1, MAP_SELECTED, param, false); // earth.qrc
	map_vsh_resource(2, MAP_SELECTED, param, false); // canyon.qrc
	map_vsh_resource(3, MAP_SELECTED, param, false); // lines.qrc
//	map_vsh_resource(7, MAP_SELECTED, param, false); // gameboot (custom_render_plugin.rco + gameboot_stereo.ac3/gameboot_multi.ac3)
	if(!apply_theme) return;
	map_vsh_resource(5, MAP_SELECTED, param, false); // theme.p3t
}
#endif // #ifdef VISUALIZERS

#ifdef MOUNT_ROMS
static void copy_rom_media(char *src_path)
{
	// get rom name & file extension
	char *name = get_filename(src_path);
	if(!name) return;

	char dst_path[64];

	char *ext  = strrchr(++name, '.');
	if(ext)
	{
		char path[MAX_LINE_LEN];

		// copy rom icon to ICON0.PNG
		sprintf(dst_path, "%s/PS3_GAME/%s", PKGLAUNCH_DIR, "ICON0.PNG");
		{strcpy(ext, ".png"); if(file_exists(src_path)) {file_copy(src_path, dst_path, COPY_WHOLE_FILE);} else
		{strcpy(ext, ".PNG"); if(file_exists(src_path)) {file_copy(src_path, dst_path, COPY_WHOLE_FILE);} else
		{
			sprintf(path, "%s/%s", WMTMP, name);
			char *ext2 = strrchr(path, '.');
			{strcpy(ext2, ".png"); if(file_exists(path)) {file_copy(path, dst_path, COPY_WHOLE_FILE);} else
			{strcpy(ext2, ".PNG"); if(file_exists(path)) {file_copy(path, dst_path, COPY_WHOLE_FILE);} else
														 {file_copy((char*)PKGLAUNCH_ICON, dst_path, COPY_WHOLE_FILE);}}}
		}}}

		strcpy(path, src_path); char *path_ = get_filename(path) + 1;

		const char *media[5] = {"PIC0.PNG", "PIC1.PNG", "PIC2.PNG", "SND0.AT3", "ICON1.PAM"};
		for(u8 i = 0; i < 5; i++)
		{
			sprintf(dst_path, "%s/PS3_GAME/%s", PKGLAUNCH_DIR, media[i]); cellFsUnlink(dst_path);
			strcpy(ext + 1, media[i]);
			if(file_exists(src_path))
				file_copy(src_path, dst_path, COPY_WHOLE_FILE);
			else
			{
				strcpy(path_, media[i]);
				if(file_exists(path))
					file_copy(path, dst_path, COPY_WHOLE_FILE);
			}
		}
		*ext = NULL;
	}

	// patch title name in PARAM.SFO of PKGLAUNCH
	sprintf(dst_path, "%s/PS3_GAME/%s", PKGLAUNCH_DIR, "PARAM.SFO");
	write_file(dst_path, CELL_FS_O_CREAT | CELL_FS_O_WRONLY, name, 0x378, 0x80, false);
}
#endif // #ifdef MOUNT_ROMS

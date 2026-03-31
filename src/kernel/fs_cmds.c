/**
 * @file fs_cmds.c
 * @brief ULFS Shell Command Implementations
 *
 * Connects the ULFS file system API to the MiniOS interactive shell.
 * Each handler follows the cmd_handler_t signature: void fn(int argc, char *argv[]).
 *
 * All I/O goes through HAL_UART_PutString() (no printf, no stdlib).
 *
 * @note SRS FR-023 (system status), DC-001 (C11, no stdlib).
 */

#include "kernel/fs_cmds.h"
#include "kernel/ulfs.h"
#include "kernel/cmd.h"
#include "hal/uart.h"
#include "lib/string.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/** Print a decimal number without printf */
static void print_dec(uint32_t v)
{
    HAL_UART_PutDec(v);
}

/** Print a right-aligned number in a field of `width` characters */
static void print_dec_padded(uint32_t v, uint32_t width)
{
    /* Determine number of digits */
    uint32_t tmp = v, digits = 1;
    while (tmp >= 10) { tmp /= 10; digits++; }
    for (uint32_t p = digits; p < width; p++) HAL_UART_PutChar(' ');
    print_dec(v);
}

/** Simple strlen without standard library */
static uint32_t fs_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ------------------------------------------------------------------ */
/*  ls — list directory                                               */
/* ------------------------------------------------------------------ */

/** Callback data used during ls readdir iteration */
typedef struct {
    uint32_t count;
} ls_data_t;

/**
 * @brief Readdir callback that prints one directory entry.
 *
 * Format:  [d] dirname    or   [f] filename  12345 B
 */
static void ls_entry_cb(const char *name, const ulfs_stat_t *st, void *user_data)
{
    ls_data_t *ld = (ls_data_t *)user_data;
    ld->count++;

    if (st->type == ULFS_TYPE_DIR) {
        HAL_UART_PutString("  [d]  ");
        HAL_UART_PutString(name);
        HAL_UART_PutString("/\n");
    } else {
        HAL_UART_PutString("  [f]  ");
        /* Left-pad name to 20 chars */
        HAL_UART_PutString(name);
        uint32_t namelen = fs_strlen(name);
        for (uint32_t i = namelen; i < 20; i++) HAL_UART_PutChar(' ');
        HAL_UART_PutString("  ");
        print_dec(st->size);
        HAL_UART_PutString(" B\n");
    }
}

/**
 * @brief ls [path]
 *
 * Lists the contents of the given path (or CWD if no argument).
 * Shows directories with trailing '/' and files with their byte size.
 *
 * Usage:
 *   ls
 *   ls /subdir
 */
static void cmd_ls(int argc, char *argv[])
{
    const char *path = (argc >= 2) ? argv[1] : ".";

    ls_data_t ld = { 0 };
    Status s = ULFS_Readdir(path, ls_entry_cb, &ld);

    if (s != STATUS_OK) {
        HAL_UART_PutString("ls: cannot access '");
        HAL_UART_PutString(path);
        HAL_UART_PutString("': no such directory\n");
        return;
    }

    if (ld.count == 0) {
        HAL_UART_PutString("  (empty)\n");
    }
}

/* ------------------------------------------------------------------ */
/*  cd — change directory                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief cd <path>
 *
 * Changes the current working directory. Supports absolute and relative paths,
 * and the special "." and ".." entries.
 *
 * Usage:
 *   cd /
 *   cd subdir
 *   cd ..
 */
static void cmd_cd(int argc, char *argv[])
{
    if (argc < 2) {
        /* cd with no args goes to root */
        ULFS_ChDir("/");
        return;
    }

    Status s = ULFS_ChDir(argv[1]);

    if (s != STATUS_OK) {
        HAL_UART_PutString("cd: '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("': no such directory\n");
    }
}

/* ------------------------------------------------------------------ */
/*  pwd — print working directory                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief pwd
 *
 * Prints the absolute path of the current working directory.
 */
static void cmd_pwd(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    char buf[ULFS_PATH_MAX];
    ULFS_GetCwd(buf, sizeof(buf));
    HAL_UART_PutString(buf);
    HAL_UART_PutString("\n");
}

/* ------------------------------------------------------------------ */
/*  mkdir — create directory                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief mkdir <name>
 *
 * Creates a new directory with the given name as a child of the CWD
 * (or any absolute path).
 *
 * Usage:
 *   mkdir mydir
 *   mkdir /data
 */
static void cmd_mkdir(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: mkdir <dirname>\n");
        return;
    }

    Status s = ULFS_Mkdir(argv[1]);

    if (s == STATUS_OK) {
        HAL_UART_PutString("mkdir: created directory '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("'\n");
    } else if (s == STATUS_ERROR_INVALID_ARGUMENT) {
        HAL_UART_PutString("mkdir: '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("': already exists\n");
    } else {
        HAL_UART_PutString("mkdir: failed (disk full or invalid path)\n");
    }
    
    ULFS_Sync();
}

/* ------------------------------------------------------------------ */
/*  touch — create empty file                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief touch <filename>
 *
 * Creates an empty regular file. Does nothing if the file already exists.
 *
 * Usage:
 *   touch myfile.txt
 */
static void cmd_touch(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: touch <filename>\n");
        return;
    }

    Status s = ULFS_Create(argv[1]);

    if (s == STATUS_OK) {
        HAL_UART_PutString("touch: created '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("'\n");
    } else if (s == STATUS_ERROR_INVALID_ARGUMENT) {
        /* Already exists — silently succeed (POSIX touch behavior) */
    } else {
        HAL_UART_PutString("touch: failed to create file\n");
    }
    
    ULFS_Sync();
}

/* ------------------------------------------------------------------ */
/*  cat — print file contents                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief cat <filename>
 *
 * Reads and prints the contents of a file to UART. Non-printable bytes
 * are shown as '.' for safety.
 *
 * Usage:
 *   cat myfile.txt
 */
static void cmd_cat(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: cat <filename>\n");
        return;
    }

    int fd;
    Status s = ULFS_Open(argv[1], ULFS_O_RDONLY, &fd);

    if (s != STATUS_OK) {
        HAL_UART_PutString("cat: '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("': no such file\n");
        return;
    }

    uint8_t buf[64];
    uint32_t nread;
    bool printed_anything = false;

    while (1) {
        s = ULFS_Read(fd, buf, sizeof(buf), &nread);
        if (s != STATUS_OK || nread == 0) break;
        printed_anything = true;

        for (uint32_t i = 0; i < nread; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r' || (c >= 0x20 && c < 0x7F)) {
                HAL_UART_PutChar(c);
            } else {
                HAL_UART_PutChar('.');
            }
        }
    }

    if (!printed_anything) {
        HAL_UART_PutString("(empty file)\n");
    } else {
        HAL_UART_PutString("\n");
    }

    ULFS_Close(fd);
}

/* ------------------------------------------------------------------ */
/*  write — write text to a file                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief write <filename> <text...>
 *
 * Creates or overwrites the named file with the text given as remaining
 * arguments (space-separated, a single newline is appended).
 *
 * Usage:
 *   write hello.txt Hello, World!
 *   write log.txt inference completed
 */
static void cmd_write(int argc, char *argv[])
{
    if (argc < 3) {
        HAL_UART_PutString("usage: write <filename> <text...>\n");
        return;
    }

    int fd;
    /* Open with O_CREAT | O_TRUNC | O_WRONLY */
    Status s = ULFS_Open(argv[1], ULFS_O_CREAT | ULFS_O_TRUNC | ULFS_O_WRONLY, &fd);

    if (s != STATUS_OK) {
        HAL_UART_PutString("write: cannot open '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("'\n");
        return;
    }

    uint32_t nwritten;

    /* Write each arg (argv[2..argc-1]) separated by spaces */
    for (int i = 2; i < argc; i++) {
        uint32_t len = (uint32_t)fs_strlen(argv[i]);
        s = ULFS_Write(fd, argv[i], len, &nwritten);
        if (s != STATUS_OK) break;

        if (i < argc - 1) {
            ULFS_Write(fd, " ", 1, &nwritten);
        }
    }

    /* Append newline */
    ULFS_Write(fd, "\n", 1, &nwritten);

    ULFS_Close(fd);

    HAL_UART_PutString("write: '");
    HAL_UART_PutString(argv[1]);
    HAL_UART_PutString("' written\n");
    
    ULFS_Sync();
}

/* ------------------------------------------------------------------ */
/*  rm — remove file or empty directory                               */
/* ------------------------------------------------------------------ */

/**
 * @brief rm <name>
 *
 * Removes a file or an empty directory. Refuses to remove non-empty
 * directories.
 *
 * Usage:
 *   rm myfile.txt
 *   rm emptydir
 */
static void cmd_rm(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: rm <name>\n");
        return;
    }

    Status s = ULFS_Unlink(argv[1]);

    if (s == STATUS_OK) {
        HAL_UART_PutString("rm: removed '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("'\n");
    } else if (s == STATUS_ERROR_NOT_SUPPORTED) {
        HAL_UART_PutString("rm: '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("': directory not empty\n");
    } else {
        HAL_UART_PutString("rm: '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("': no such file or directory\n");
    }
    
    ULFS_Sync();
}

/* ------------------------------------------------------------------ */
/*  cp — copy file                                                    */
/* ------------------------------------------------------------------ */

static void cmd_cp(int argc, char *argv[])
{
    if (argc < 3) {
        HAL_UART_PutString("usage: cp <source> <destination>\n");
        return;
    }

    int fd_src, fd_dst;
    Status s = ULFS_Open(argv[1], ULFS_O_RDONLY, &fd_src);
    if (s != STATUS_OK) {
        HAL_UART_PutString("cp: cannot open '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("' for reading\n");
        return;
    }

    s = ULFS_Open(argv[2], ULFS_O_CREAT | ULFS_O_TRUNC | ULFS_O_WRONLY, &fd_dst);
    if (s != STATUS_OK) {
        HAL_UART_PutString("cp: cannot create '");
        HAL_UART_PutString(argv[2]);
        HAL_UART_PutString("'\n");
        ULFS_Close(fd_src);
        return;
    }

    uint8_t buf[256];
    uint32_t nread, nwritten;
    while (1) {
        s = ULFS_Read(fd_src, buf, sizeof(buf), &nread);
        if (s != STATUS_OK || nread == 0) break;

        s = ULFS_Write(fd_dst, buf, nread, &nwritten);
        if (s != STATUS_OK || nwritten < nread) {
            HAL_UART_PutString("cp: write failed\n");
            break;
        }
    }

    ULFS_Close(fd_src);
    ULFS_Close(fd_dst);

    HAL_UART_PutString("cp: copied '");
    HAL_UART_PutString(argv[1]);
    HAL_UART_PutString("' to '");
    HAL_UART_PutString(argv[2]);
    HAL_UART_PutString("'\n");
    
    ULFS_Sync();
}

/* ------------------------------------------------------------------ */
/*  mv — move file                                                    */
/* ------------------------------------------------------------------ */

static void cmd_mv(int argc, char *argv[])
{
    if (argc < 3) {
        HAL_UART_PutString("usage: mv <source> <destination>\n");
        return;
    }

    int fd_src, fd_dst;
    Status s = ULFS_Open(argv[1], ULFS_O_RDONLY, &fd_src);
    if (s != STATUS_OK) {
        HAL_UART_PutString("mv: cannot read '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("'\n");
        return;
    }

    s = ULFS_Open(argv[2], ULFS_O_CREAT | ULFS_O_TRUNC | ULFS_O_WRONLY, &fd_dst);
    if (s != STATUS_OK) {
        HAL_UART_PutString("mv: cannot create '");
        HAL_UART_PutString(argv[2]);
        HAL_UART_PutString("'\n");
        ULFS_Close(fd_src);
        return;
    }

    uint8_t buf[256];
    uint32_t nread, nwritten;
    while (1) {
        s = ULFS_Read(fd_src, buf, sizeof(buf), &nread);
        if (s != STATUS_OK || nread == 0) break;

        s = ULFS_Write(fd_dst, buf, nread, &nwritten);
        if (s != STATUS_OK || nwritten < nread) break;
    }

    ULFS_Close(fd_src);
    ULFS_Close(fd_dst);

    /* Original successfully copied, now remove original */
    ULFS_Unlink(argv[1]);

    HAL_UART_PutString("mv: moved '");
    HAL_UART_PutString(argv[1]);
    HAL_UART_PutString("' to '");
    HAL_UART_PutString(argv[2]);
    HAL_UART_PutString("'\n");
    
    ULFS_Sync();
}

/* ------------------------------------------------------------------ */
/*  stat — show inode information                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief stat <name>
 *
 * Prints the inode number, file type, size in bytes, and number of
 * 4 KB blocks consumed.
 *
 * Usage:
 *   stat myfile.txt
 *   stat /
 */
static void cmd_stat(int argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: stat <name>\n");
        return;
    }

    ulfs_stat_t st;
    Status s = ULFS_Stat(argv[1], &st);

    if (s != STATUS_OK) {
        HAL_UART_PutString("stat: '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("': no such file or directory\n");
        return;
    }

    HAL_UART_PutString("  File   : ");
    HAL_UART_PutString(argv[1]);
    HAL_UART_PutString("\n");

    HAL_UART_PutString("  Inode  : ");
    print_dec(st.ino);
    HAL_UART_PutString("\n");

    HAL_UART_PutString("  Type   : ");
    HAL_UART_PutString(st.type == ULFS_TYPE_DIR ? "directory" : "regular file");
    HAL_UART_PutString("\n");

    HAL_UART_PutString("  Size   : ");
    print_dec(st.size);
    HAL_UART_PutString(" bytes\n");

    HAL_UART_PutString("  Blocks : ");
    print_dec(st.blocks);
    HAL_UART_PutString(" × 4 KB\n");
}

/* ------------------------------------------------------------------ */
/*  df — disk usage                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief df
 *
 * Prints the total, used, and free block counts. Each block is 4 KB.
 *
 * Usage:
 *   df
 */
static void cmd_df(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    uint32_t total, used, free_blks;
    ULFS_GetStats(&total, &used, &free_blks);

    HAL_UART_PutString("  ULFS Filesystem Usage\n");
    HAL_UART_PutString("  ----------------------\n");

    HAL_UART_PutString("  Total : ");
    print_dec_padded(total, 4);
    HAL_UART_PutString(" blocks  (");
    print_dec(total * 4);
    HAL_UART_PutString(" KB)\n");

    HAL_UART_PutString("  Used  : ");
    print_dec_padded(used, 4);
    HAL_UART_PutString(" blocks  (");
    print_dec(used * 4);
    HAL_UART_PutString(" KB)\n");

    HAL_UART_PutString("  Free  : ");
    print_dec_padded(free_blks, 4);
    HAL_UART_PutString(" blocks  (");
    print_dec(free_blks * 4);
    HAL_UART_PutString(" KB)\n");

    if (total > 0) {
        uint32_t pct = (used * 100U) / total;
        HAL_UART_PutString("  Usage : ");
        print_dec(pct);
        HAL_UART_PutString("%\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status FS_RegisterCommands(void)
{
    Status s;

    s = CMD_Register("ls",    "List directory contents [path]",         cmd_ls);
    if (s != STATUS_OK) return s;

    s = CMD_Register("cd",    "Change working directory <path>",         cmd_cd);
    if (s != STATUS_OK) return s;

    s = CMD_Register("pwd",   "Print working directory",                 cmd_pwd);
    if (s != STATUS_OK) return s;

    s = CMD_Register("mkdir", "Create a directory <name>",               cmd_mkdir);
    if (s != STATUS_OK) return s;

    s = CMD_Register("touch", "Create an empty file <name>",             cmd_touch);
    if (s != STATUS_OK) return s;

    s = CMD_Register("cat",   "Show file contents <filename>",           cmd_cat);
    if (s != STATUS_OK) return s;

    s = CMD_Register("write", "Write text to a file <file> <text...>",   cmd_write);
    if (s != STATUS_OK) return s;

    s = CMD_Register("rm",    "Remove file or empty directory <name>",   cmd_rm);
    if (s != STATUS_OK) return s;

    s = CMD_Register("cp",    "Copy file <src> <dst>",                   cmd_cp);
    if (s != STATUS_OK) return s;

    s = CMD_Register("mv",    "Move file <src> <dst>",                   cmd_mv);
    if (s != STATUS_OK) return s;

    s = CMD_Register("stat",  "Show inode info <name>",                  cmd_stat);
    if (s != STATUS_OK) return s;

    s = CMD_Register("df",    "Show file system disk usage",             cmd_df);
    if (s != STATUS_OK) return s;

    return STATUS_OK;
}

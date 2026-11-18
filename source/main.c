// Mii Plaza Probe - READ-ONLY.
// Lists the StreetPass (CEC) mailboxes on this 3DS, shows how many messages
// each inbox holds, and saves the first message of each non-empty inbox to
// the SD card. It never writes to the console's StreetPass data.
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <sys/stat.h>

#define PATH_MBOXLIST  1
#define PATH_INBOXINFO 3
#define MAX_DUMPS      6

static Handle cecd = 0;
static FILE *logf = NULL;
static u32 flag_used = 1;

static void say(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s", buf);
    if (logf) { fputs(buf, logf); fflush(logf); }
}

// CECDU:OpenAndRead (0x00120104): read a CEC system-save file by path type.
static Result cec_open_read(u32 programId, u32 pathType, u32 flag, void *buf, u32 size, u32 *outRead)
{
    u32 *c = getThreadCommandBuffer();
    c[0] = 0x00120104;
    c[1] = size;
    c[2] = programId;
    c[3] = pathType;
    c[4] = flag;
    c[5] = IPC_Desc_CurProcessId();
    c[6] = 0;
    c[7] = IPC_Desc_Buffer(size, IPC_BUFFER_W);
    c[8] = (u32)buf;
    Result r = svcSendSyncRequest(cecd);
    if (R_FAILED(r)) return r;
    if (outRead) *outRead = c[2];
    return (Result)c[1];
}

// CECDU:ReadMessage (0x00030104): read one message (inbox) by 8-byte id.
static Result cec_read_message(u32 boxId, bool outBox, const u8 *msgId, void *buf, u32 size, u32 *outRead)
{
    u32 *c = getThreadCommandBuffer();
    c[0] = 0x00030104;
    c[1] = boxId;
    c[2] = outBox ? 1 : 0;
    c[3] = 8;
    c[4] = size;
    c[5] = IPC_Desc_Buffer(8, IPC_BUFFER_R);
    c[6] = (u32)msgId;
    c[7] = IPC_Desc_Buffer(size, IPC_BUFFER_W);
    c[8] = (u32)buf;
    Result r = svcSendSyncRequest(cecd);
    if (R_FAILED(r)) return r;
    if (outRead) *outRead = c[2];
    return (Result)c[1];
}

// Try a few harmless "read" open flags until one works.
static Result read_with_flags(u32 pid, u32 pathType, void *buf, u32 size, u32 *outRead)
{
    static const u32 flags[] = { 1, 0 };
    Result last = 0;
    for (unsigned i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        memset(buf, 0, size);
        last = cec_open_read(pid, pathType, flags[i], buf, size, outRead);
        if (R_SUCCEEDED(last)) { flag_used = flags[i]; return 0; }
        say("  open flag %lu -> 0x%08lX\n", flags[i], (unsigned long)last);
    }
    return last;
}

static void hexline(const u8 *p, int n)
{
    for (int i = 0; i < n; i++) say("%02X", p[i]);
    say("\n");
}

static void wait_exit(void)
{
    say("\nPress START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

int main(void)
{
    mkdir("sdmc:/miiplaza_probe", 0777);
    logf = fopen("sdmc:/miiplaza_probe/log.txt", "w");
    say("main() started\n");
    gfxInitDefault();
    say("gfx init ok\n");
    consoleInit(GFX_TOP, NULL);
    say("console init ok\n");

    say("Mii Plaza Probe (read-only)\n");
    say("---------------------------\n");
    say("A = probe CEC (read-only)\n");
    say("START = exit\n\n");
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_START) goto done;
        if (k & KEY_A) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    say("A pressed, connecting...\n");
    gfxFlushBuffers();
    gfxSwapBuffers();

    Result rc = srvGetServiceHandle(&cecd, "cecd:u");
    if (R_FAILED(rc)) {
        say("cecd:u failed 0x%08lX, trying cecd:s\n", (unsigned long)rc);
        rc = srvGetServiceHandle(&cecd, "cecd:s");
    }
    if (R_FAILED(rc)) {
        say("No CEC service access: 0x%08lX\n", (unsigned long)rc);
        wait_exit();
        goto done;
    }
    say("CEC service OK\n");

    u8 *list = (u8 *)memalign(0x1000, 0x1000);
    u8 *info = (u8 *)memalign(0x1000, 0x8000);
    u8 *msg  = (u8 *)memalign(0x1000, 0x20000);
    if (!list || !info || !msg) {
        say("Out of memory\n");
        wait_exit();
        goto done;
    }

    u32 got = 0;
    rc = read_with_flags(0, PATH_MBOXLIST, list, 0x1000, &got);
    if (R_FAILED(rc)) {
        say("Reading mailbox list failed: 0x%08lX\n", (unsigned long)rc);
        wait_exit();
        goto done;
    }
    u32 nboxes = *(u32 *)(list + 8);
    say("Mailbox list read (%lu bytes), %lu boxes\n", (unsigned long)got, (unsigned long)nboxes);
    say("list header: ");
    hexline(list, 16);
    if (nboxes > 24) nboxes = 24;

    int dumps = 0;
    for (u32 b = 0; b < nboxes; b++) {
        char name[17];
        memcpy(name, list + 0x0C + b * 16, 16);
        name[16] = 0;
        if (!name[0]) continue;
        u32 boxId = (u32)strtoul(name, NULL, 16);

        got = 0;
        rc = read_with_flags(boxId, PATH_INBOXINFO, info, 0x8000, &got);
        if (R_FAILED(rc)) {
            say("[%s] inbox info: 0x%08lX\n", name, (unsigned long)rc);
            continue;
        }
        u32 count = *(u32 *)(info + 0x14);
        say("[%s] inbox messages: %lu\n", name, (unsigned long)count);
        if (count == 0 || count > 200) continue;
        if (dumps >= MAX_DUMPS) continue;

        const u8 *entry = info + 0x20;  // first 0x70-byte message header copy
        u32 msgSize = *(u32 *)(entry + 4);
        say("  first msg size 0x%lX, id ", (unsigned long)msgSize);
        hexline(entry + 0x20, 8);

        u8 id[8] __attribute__((aligned(8)));
        memcpy(id, entry + 0x20, 8);
        u32 rd = 0;
        rc = cec_read_message(boxId, false, id, msg, 0x20000, &rd);
        if (R_FAILED(rc)) {
            say("  read message failed: 0x%08lX\n", (unsigned long)rc);
            continue;
        }
        char path[96];
        snprintf(path, sizeof(path), "sdmc:/miiplaza_probe/msg_%s_0.bin", name);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(msg, 1, rd, f); fclose(f); say("  saved %lu bytes\n", (unsigned long)rd); dumps++; }
        else say("  could not write %s\n", path);

        snprintf(path, sizeof(path), "sdmc:/miiplaza_probe/boxinfo_%s.bin", name);
        f = fopen(path, "wb");
        if (f) { fwrite(info, 1, got < 0x8000 ? got : 0x8000, f); fclose(f); }
    }

    say("\nDone. Files are in sdmc:/miiplaza_probe/\n");
    say("(open flag used: %lu)\n", (unsigned long)flag_used);
    wait_exit();

done:
    if (logf) fclose(logf);
    if (cecd) svcCloseHandle(cecd);
    gfxExit();
    return 0;
}

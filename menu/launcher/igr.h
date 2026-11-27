/* menu/launcher/igr.h -- in-game reset ("soft reset") back to Aether (M6 Task 20a). The game's own Sega-standard
 * exit (A+B+C+Start at gameplay -> title, again at the title) calls the BIOS service SYS_EXECDMP through the pointer
 * at 0x0600026C; we point it at a small RAM stub that stops the Satiator's emulation and resets the console
 * (the igr=e sequence proven on hardware, progress.md "HARDWARE, igr=e round 2"). Technique: SAROO (tpunix,
 * GPL-3.0), PSKai (cafe-alpha, GPL-2.0); no code copied. Evidence for every address: task-20a-report.md.
 *
 * Arming chain (BIOS v1.00-v1.03 retail, measured in the ROM images):
 *  1. before the loadcd_boot loop the launcher sets the BIOS copy vector *(0x06000238) = om_igr_w (LWRAM);
 *  2. the BIOS calls it only from loadcd_boot's last step (ROM 0x18D2), after the IP checks and before the warm
 *     re-init at ROM 0x444/0x252 that restores 0x06000220-0x06000AFF (incl. 0x26C, 0x284, 0x238) and clears LWRAM
 *     and HWRAM >= 0x0600A000; om_igr_w copies the hook + stub image to OM_IGR_STAGE (survives the re-init) and
 *     points the security code's read-vector cell 0x06002270 at it;
 *  3. the security code calls the hook instead of *(0x06000284), after the logo is decompressed and just before
 *     the game's first read; the hook copies the stub to OM_IGR_STUB (the dead logo data), sets 0x0600026C and
 *     tail-jumps to the BIOS's own read. BSD-3-Clause. */
#ifndef OM_IGR_H
#define OM_IGR_H
#include <stdint.h>

#define OM_IGR_EXECDMP      0x0600026Cu   /* SYS_EXECDMP pointer (SEGA_SYS.H) */
#define OM_IGR_READ1ST      0x06000284u   /* BIOS "read first program" vector (= 0x1C90 on every retail BIOS) */
#define OM_IGR_SEC_CELL     0x06002270u   /* security-code literal: the address of the read vector (IP + 0x270) */
#define OM_IGR_COPYVEC      0x06000238u   /* BIOS copy-routine vector (= 0x2BC), called by loadcd_boot's last step */
#define OM_IGR_IP           0x06002000u
#define OM_IGR_STUB         0x06002800u   /* stub home: dead compressed-logo data, IP + 0x800 (Kai location code) */
#define OM_IGR_STUB_END     0x06002D48u   /* Kai's documented end; 0x06002D7C.. is live after the read */
#define OM_IGR_STAGE        0x06009800u   /* hook + stub image between W and the hook: < 0x0600A000 (not cleared) */
#define OM_IGR_STAGE_END    0x0600A000u
/* SYS_EXECDMP's guard (igrguard.c; Astal, 2026-11-23): 0x0600026C points here, not at the stub. It enters the stub
 * only while the stub home OM_IGR_STUB..OM_IGR_STUB_END sums to OM_IGR_SEAL, and otherwise resets the console. Its
 * home is the BIOS copyright text 0x060003C0-0x060003FF (HWF-80). The seal word is the home's last long. */
#define OM_IGR_GUARD        0x060003C0u
#define OM_IGR_GUARD_END    0x06000400u
#define OM_IGR_SEAL         0xFFFFFFA5u   /* not 0 and not -1, so a zero- or 0xFF-filled home never passes */
/* The BIOS's own SYS_EXECDMP, for a lid exit (igrstub.c's entry): the value to record from *(0x0600026C) as the BIOS
 * left it -- kept only when it points into the BIOS ROM (0x00000400..0x0007FFFF), else 0 (the entry then always runs
 * the in-game reset). Pure; host-tested. */
uint32_t om_igr_bios_execdmp_of(uint32_t v);

/* om_igr_ip_check: why IGR cannot be armed for this IP (0 = it can). Pure; host-tested. */
enum { OM_IGR_OK = 0, OM_IGR_E_OFF = 1, OM_IGR_E_HEADER = 2, OM_IGR_E_SECWORD = 3, OM_IGR_E_IPSIZE = 4 };
int om_igr_ip_check(const uint8_t *ip, uint32_t stage_bytes);
const char *om_igr_strerror(int why);
/* om_igr_seal: sets img[longs - 1] so the `longs` words of img sum to OM_IGR_SEAL; returns OM_IGR_SEAL. Run on the
 * staged stub image (OM_IGR_STUB..OM_IGR_STUB_END) before it is copied home. Pure; host-tested. */
uint32_t om_igr_seal(uint32_t *img, uint32_t longs);
void om_igr_guard(void);           /* the guard (igrguard.c); never returns on the Saturn */

/* The stub's sequence, a plain function so a host test can run it against a fake register bank (IGR_HOST_TEST).
 * On the Saturn it never returns (SYSRES). */
void om_igr_run(void);

/* Launcher side (igr.c; not in host builds). */
int om_igr_install(void);          /* checks the IP at 0x06002000 and sets *(0x06000238); returns OM_IGR_* */
void om_igr_uninstall(void);       /* loadcd_boot returned (failure): put 0x06000238 back */
#endif

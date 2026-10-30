/* fisix_legal.h - Legal notice text for Terminal / About.
 * License: GPLv2. See LICENSE / LICENSES/.
 * Call fisix_legal_text() to get a short, displayable notice.
 * Full texts live in LICENSES/ and in apps/legal.txt in the source
 * tree; the ISO build should include legal.txt alongside the kernel
 * so users can read it without a source checkout.
 */
#ifndef FISIX_LEGAL_H
#define FISIX_LEGAL_H
const char *fisix_legal_text(void);
#endif

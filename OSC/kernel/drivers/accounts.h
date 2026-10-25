#ifndef FISIX_ACCOUNTS_H
#define FISIX_ACCOUNTS_H

#include "../../boot/efi.h"

#define FISIX_MAX_USERS 8

void accounts_init(void);
int accounts_first_boot(void);
int accounts_setup_first_user(const CHAR16 *name, const CHAR16 *password);
int accounts_login(const CHAR16 *name, const CHAR16 *password);
int accounts_add(const CHAR16 *name, const CHAR16 *password);
int accounts_change_password(const CHAR16 *old_password, const CHAR16 *new_password);
int accounts_is_admin(void);
const CHAR16 *accounts_current_user(void);
int accounts_count(void);
const CHAR16 *accounts_user_at(int index);

#endif

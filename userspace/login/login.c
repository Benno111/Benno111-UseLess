/*
 * vib-OS - Login Program
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../shared/password_hash.h"

#define ACCOUNT_CONFIG_PATH "/System/account.cfg"
#define MAX_USER_LEN 32
#define MAX_PASS_LEN 32
#define MAX_HOME_LEN 96
#define MAX_SHELL_LEN 32

struct user_cred {
  char username[MAX_USER_LEN];
  char password_hash[33];
  uid_t uid;
  gid_t gid;
  char home[MAX_HOME_LEN];
  char shell[MAX_SHELL_LEN];
  int configured;
};

static void trim_newline(char *buf) {
  size_t len;

  if (!buf)
    return;

  len = strlen(buf);
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
    buf[len - 1] = '\0';
    len--;
  }
}

static void build_default_home(const char *username, char *out, size_t len) {
  if (!out || len == 0)
    return;

  if (username && strcmp(username, "root") == 0) {
    snprintf(out, len, "/root");
    return;
  }

  snprintf(out, len, "/Users/%s", username ? username : "user");
}

static int manifest_get_value(const char *manifest, const char *key, char *out,
                              size_t max) {
  size_t key_len = 0;
  size_t i = 0;

  if (!manifest || !key || !out || max == 0)
    return -1;

  while (key[key_len])
    key_len++;

  while (manifest[i]) {
    size_t j = 0;

    while (j < key_len && manifest[i + j] == key[j])
      j++;
    if (j == key_len && manifest[i + j] == '=') {
      size_t out_idx = 0;
      i += key_len + 1;
      while (manifest[i] && manifest[i] != '\n' && manifest[i] != '\r' &&
             out_idx < max - 1) {
        out[out_idx++] = manifest[i++];
      }
      out[out_idx] = '\0';
      return 0;
    }

    while (manifest[i] && manifest[i] != '\n')
      i++;
    if (manifest[i] == '\n')
      i++;
  }

  out[0] = '\0';
  return -1;
}

static int load_system_account(struct user_cred *cred) {
  FILE *fp;
  char manifest[192];
  size_t bytes;
  char legacy_password[MAX_PASS_LEN];

  if (!cred)
    return -1;

  memset(cred, 0, sizeof(*cred));
  strcpy(cred->shell, "/bin/sh");

  fp = fopen(ACCOUNT_CONFIG_PATH, "r");
  if (!fp)
    return -1;

  bytes = fread(manifest, 1, sizeof(manifest) - 1, fp);
  fclose(fp);
  if (bytes == 0)
    return -1;
  manifest[bytes] = '\0';

  if (manifest_get_value(manifest, "username", cred->username,
                         sizeof(cred->username)) != 0 ||
      !cred->username[0]) {
    return -1;
  }

  if (manifest_get_value(manifest, "password_hash", cred->password_hash,
                         sizeof(cred->password_hash)) != 0 ||
      !cred->password_hash[0]) {
    legacy_password[0] = '\0';
    if (manifest_get_value(manifest, "password", legacy_password,
                           sizeof(legacy_password)) != 0 ||
        !legacy_password[0]) {
      return -1;
    }
    vib_password_hash_hex(cred->username, legacy_password, cred->password_hash,
                          sizeof(cred->password_hash));
  }

  build_default_home(cred->username, cred->home, sizeof(cred->home));
  cred->uid = strcmp(cred->username, "root") == 0 ? 0 : 1000;
  cred->gid = cred->uid;
  cred->configured = 1;
  return 0;
}

static void get_password(char *buf, size_t len) {
  printf("(visible) ");
  if (!fgets(buf, (int)len, stdin)) {
    if (len > 0)
      buf[0] = '\0';
    return;
  }
  trim_newline(buf);
}

static int authenticate(const struct user_cred *cred, const char *user,
                        const char *pass) {
  char password_hash[33];

  if (!cred || !cred->configured || !user || !pass)
    return 0;
  if (strcmp(user, cred->username) != 0)
    return 0;

  vib_password_hash_hex(user, pass, password_hash, sizeof(password_hash));
  return vib_secure_string_eq(password_hash, cred->password_hash);
}

int main(int argc, char *argv[]) {
  char username[MAX_USER_LEN];
  char password[MAX_PASS_LEN];
  struct user_cred cred;

  (void)argc;
  (void)argv;

  printf("\033[2J\033[H");
  printf("Welcome to vib-OS v8.0.0\n");
  printf("Kernel 8.0.0-arm64 on an aarch64\n\n");

  while (1) {
    if (load_system_account(&cred) != 0) {
      printf("No configured account found. Run setup first.\n");
      sleep(2);
      continue;
    }

    printf("vib-os login: ");
    fflush(stdout);

    if (fgets(username, sizeof(username), stdin) == NULL)
      continue;
    trim_newline(username);

    if (strlen(username) == 0)
      continue;

    printf("Password: ");
    fflush(stdout);
    get_password(password, sizeof(password));

    if (authenticate(&cred, username, password)) {
      char home_env[MAX_HOME_LEN + 5];
      char shell_env[MAX_SHELL_LEN + 7];
      char *shell_argv[] = {cred.shell, "-l", NULL};
      char *envp[] = {home_env, "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
                      shell_env, "TERM=linux", NULL};

      snprintf(home_env, sizeof(home_env), "HOME=%s", cred.home);
      snprintf(shell_env, sizeof(shell_env), "SHELL=%s", cred.shell);

      printf("\nLogin successful.\n\n");
      chdir(cred.home);
      execve(cred.shell, shell_argv, envp);

      printf("Failed to execute shell: %s\n", cred.shell);
      exit(1);
    }

    printf("\nLogin incorrect\n\n");
    sleep(2);
  }

  return 0;
}

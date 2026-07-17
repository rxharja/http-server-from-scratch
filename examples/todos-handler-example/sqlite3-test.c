#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

static sqlite3 * db;

typedef struct { const char *p; size_t n; } Str;

static bool db_init(void) {
  if (sqlite3_open("names.db", &db) != SQLITE_OK) {
    fprintf(stderr, "open: %s\n", sqlite3_errmsg(db));
    return false;
  }

  sqlite3_busy_timeout(db, 5000);

  sqlite3_exec(db, "PRAGMA journal_mode=WAL", 0, 0, 0);

  char *err = NULL;
  int rc = sqlite3_exec(db,
    "CREATE TABLE IF NOT EXISTS todos ("
    " id INTEGER PRIMARY KEY,"
    " todo text NOT NULL,"
    " completed BOOLEAN DEFAULT FALSE,"
    " created INTEGER DEFAULT (CAST(strftime('%s','now') AS INTEGER))"
    ")",0,0, &err);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "schema: %s\n", err);
    sqlite3_free(err);
    return false;
  }

  return true;
}

static bool todos_add(Str todo) {
  sqlite3_stmt *st;

  if (sqlite3_prepare_v2(db, "INSERT INTO todos(todo) VALUES(?)", -1, &st, 0) != SQLITE_OK) {
    fprintf(stderr, "prepare: %s\n", sqlite3_errmsg(db));
    return false;
  }

  sqlite3_bind_text(st, 1, todo.p, (int)todo.n, SQLITE_STATIC);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  if (!ok) fprintf(stderr, "step: %s\n", sqlite3_errmsg(db));
  sqlite3_finalize(st);

  return ok;
}

int main(void) {
  if (!db_init()) return 1;
  if (!todos_add((Str){ "Test Entry", 10})) return 1;
  printf("id=%lld\n", (long long)sqlite3_last_insert_rowid(db));
  sqlite3_close(db);
  return 0;
}

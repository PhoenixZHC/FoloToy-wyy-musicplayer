import Database from "better-sqlite3";
import { mkdirSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export type AppDatabase = Database.Database;

const currentDir = dirname(fileURLToPath(import.meta.url));
const migrationPath = resolve(currentDir, "../migrations/001_initial.sql");

export function openDb(path: string): AppDatabase {
  mkdirSync(dirname(path), { recursive: true });

  const db = new Database(path);
  db.pragma("foreign_keys = ON");
  db.exec(readFileSync(migrationPath, "utf8"));

  return db;
}

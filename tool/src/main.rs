//! katalogos: an eidos mod's release files, and the index's check.
//!
//!   katalogos pack <mod-dir> --repo <owner/name> --version <x.y.z> [--out <dir>]
//!       packs the mod folder into <out>/mod.bw and writes <out>/mod.json
//!       (docs/mod-json.md), the two files a mod's release carries.
//!   katalogos check <index.json>
//!       reads every listed mod's mod.json and mod.bw where the launcher
//!       reads them, and checks them: the format, the hash, the size, the
//!       package's own limits.  Exits 1 if any mod fails.
//!   katalogos inspect <mod.json> <mod.bw>
//!       the same checks on files already on disk.

use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

/// The mod.json format this tool writes and reads.
const FORMAT: u32 = 1;
/// The package's file name in a release.
const PACKAGE: &str = "mod.bw";
/// The cartridge codes a manifest's `game` may name (docs/mod-json.md).
const GAMES: &[(&str, &str)] = &[("IRB", "black"), ("IRA", "white")];

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct Package {
    pub file: String,
    pub sha256: String,
    pub size: u64,
    pub files: usize,
    pub unpacked: u64,
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct ModJson {
    pub format: u32,
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub description: String,
    pub repo: String,
    pub games: Vec<String>,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub game_code: String,
    #[serde(default)]
    pub tags: Vec<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub api: Option<u32>,
    pub has_code: bool,
    pub rules: usize,
    pub package: Package,
}

#[derive(Deserialize, Debug)]
pub struct Entry {
    pub repo: String,
    #[serde(default)]
    pub official: bool,
    /// A fixed version (x.y.z) instead of the repo's latest release.
    #[serde(default)]
    pub version: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct Index {
    pub format: u32,
    pub mods: Vec<Entry>,
}

fn sha256_hex(bytes: &[u8]) -> String {
    let d = Sha256::digest(bytes);
    d.iter().map(|b| format!("{b:02x}")).collect()
}

/// `owner/name`, each part made of letters, digits, `-`, `_` and `.`.
pub fn valid_repo(repo: &str) -> bool {
    let parts: Vec<&str> = repo.split('/').collect();
    parts.len() == 2
        && parts.iter().all(|p| {
            !p.is_empty() && p.chars().all(|c| c.is_ascii_alphanumeric() || "-_.".contains(c)) && *p != "." && *p != ".."
        })
}

/// x.y.z, digits only (a leading `v` is dropped).
pub fn version_of(tag: &str) -> Option<String> {
    let v = tag.trim().trim_start_matches(['v', 'V']);
    let parts: Vec<&str> = v.split('.').collect();
    if parts.len() == 3 && parts.iter().all(|p| !p.is_empty() && p.len() <= 6 && p.chars().all(|c| c.is_ascii_digit())) {
        Some(v.to_string())
    } else {
        None
    }
}

/// The manifest's own `tags = a, b` line (a key the port skips).
fn manifest_tags(other: &[String]) -> Vec<String> {
    for line in other {
        let l = line.split('#').next().unwrap_or("");
        if let Some((k, v)) = l.split_once('=') {
            if k.trim() == "tags" {
                return v
                    .split(',')
                    .map(|t| t.trim().to_lowercase())
                    .filter(|t| !t.is_empty())
                    .collect();
            }
        }
    }
    Vec::new()
}

/// How many `rule = ...` and whether a `code = ...` line the manifest has.
fn manifest_counts(text: &str) -> (usize, bool) {
    let mut rules = 0;
    let mut code = false;
    for line in text.lines() {
        let l = line.split('#').next().unwrap_or("");
        if let Some((k, v)) = l.split_once('=') {
            match k.trim() {
                "rule" if !v.trim().is_empty() => rules += 1,
                "code" if !v.trim().is_empty() => code = true,
                _ => {}
            }
        }
    }
    (rules, code)
}

fn games_of(code: &str) -> Result<Vec<String>, String> {
    if code.is_empty() {
        return Ok(GAMES.iter().map(|(_, g)| g.to_string()).collect());
    }
    for (prefix, g) in GAMES {
        if code.len() == 4 && code.starts_with(prefix) {
            return Ok(vec![g.to_string()]);
        }
    }
    Err(format!("the manifest's game = {code} is not a Black or White cartridge code (IRBx, IRAx)"))
}

/// Packs `dir` into `out/mod.bw` and writes `out/mod.json`.
pub fn pack(dir: &Path, repo: &str, version: &str, out: &Path) -> Result<ModJson, String> {
    if !valid_repo(repo) {
        return Err(format!("{repo} is not owner/name"));
    }
    let version = version_of(version).ok_or_else(|| format!("{version} is not a version (x.y.z)"))?;
    let manifest_path = dir.join(eidos_mod::manifest::MANIFEST);
    let text = fs::read_to_string(&manifest_path)
        .map_err(|e| format!("{}: {e} (a mod needs its eidos_mod.txt)", manifest_path.display()))?;
    let repo_name = repo.split('/').nth(1).unwrap_or(repo);
    let m = eidos_mod::manifest::Manifest::parse(&text, repo_name);
    if !m.version.is_empty() && version_of(&m.version).as_deref() != Some(version.as_str()) {
        return Err(format!(
            "the manifest says version = {}, the release is {version}: make them the same",
            m.version
        ));
    }
    if m.description.trim().is_empty() {
        return Err("the manifest has no description = ... line (the index shows it)".into());
    }
    let games = games_of(m.game.trim())?;
    let (rules, has_code) = manifest_counts(&text);
    fs::create_dir_all(out).map_err(|e| e.to_string())?;
    let bw = out.join(PACKAGE);
    eidos_mod::package::pack(dir, &bw).map_err(|e| format!("packing: {e}"))?;
    let info = eidos_mod::package::inspect(&bw).map_err(|e| format!("the package does not read back: {e}"))?;
    let bytes = fs::read(&bw).map_err(|e| e.to_string())?;
    let owner = repo.split('/').next().unwrap_or("");
    let j = ModJson {
        format: FORMAT,
        id: repo.to_lowercase(),
        name: m.name.clone(),
        version,
        author: if m.author.trim().is_empty() { owner.to_string() } else { m.author.trim().to_string() },
        description: m.description.trim().to_string(),
        repo: repo.to_string(),
        games,
        game_code: m.game.trim().to_string(),
        tags: manifest_tags(&m.other),
        api: m.api,
        has_code,
        rules,
        package: Package {
            file: PACKAGE.into(),
            sha256: sha256_hex(&bytes),
            size: bytes.len() as u64,
            files: info.files.len(),
            unpacked: info.files.iter().map(|f| f.size).sum(),
        },
    };
    let text = serde_json::to_string_pretty(&j).map_err(|e| e.to_string())? + "\n";
    fs::write(out.join("mod.json"), text).map_err(|e| e.to_string())?;
    Ok(j)
}

/// The checks on a mod.json and its package's bytes.
pub fn verify(j: &ModJson, bw_bytes: &[u8], tmp: &Path) -> Result<(), String> {
    if j.format != FORMAT {
        return Err(format!("mod.json format {} (this check knows {FORMAT})", j.format));
    }
    if !valid_repo(&j.repo) || j.id != j.repo.to_lowercase() {
        return Err(format!("repo {} / id {} do not agree", j.repo, j.id));
    }
    if version_of(&j.version).as_deref() != Some(j.version.as_str()) {
        return Err(format!("version {} is not x.y.z", j.version));
    }
    if j.name.trim().is_empty() || j.description.trim().is_empty() {
        return Err("no name or no description".into());
    }
    if j.games.is_empty() || j.games.iter().any(|g| g != "black" && g != "white") {
        return Err(format!("games {:?}: black and/or white", j.games));
    }
    if j.package.file != PACKAGE {
        return Err(format!("the package is {} (it must be {PACKAGE})", j.package.file));
    }
    if bw_bytes.len() as u64 != j.package.size {
        return Err(format!("mod.bw is {} bytes, mod.json says {}", bw_bytes.len(), j.package.size));
    }
    let h = sha256_hex(bw_bytes);
    if h != j.package.sha256 {
        return Err(format!("mod.bw's SHA-256 is {h}, mod.json says {}", j.package.sha256));
    }
    let p = tmp.join("check.bw");
    fs::write(&p, bw_bytes).map_err(|e| e.to_string())?;
    let info = eidos_mod::package::inspect(&p).map_err(|e| format!("mod.bw does not read as a package: {e}"))?;
    if info.files.len() != j.package.files {
        return Err(format!("mod.bw holds {} files, mod.json says {}", info.files.len(), j.package.files));
    }
    Ok(())
}

fn fetch(url: &str, max: u64) -> Result<Vec<u8>, String> {
    let resp = ureq::get(url)
        .set("User-Agent", "katalogos-check")
        .call()
        .map_err(|e| format!("{url}: {e}"))?;
    let mut buf = Vec::new();
    resp.into_reader()
        .take(max + 1)
        .read_to_end(&mut buf)
        .map_err(|e| format!("{url}: {e}"))?;
    if buf.len() as u64 > max {
        return Err(format!("{url}: larger than {max} bytes"));
    }
    Ok(buf)
}

/// Where the launcher reads a listed mod's files from.
pub fn release_url(e: &Entry, file: &str) -> String {
    match &e.version {
        Some(v) => format!("https://github.com/{}/releases/download/v{}/{file}", e.repo, v.trim_start_matches('v')),
        None => format!("https://github.com/{}/releases/latest/download/{file}", e.repo),
    }
}

fn check(index_path: &Path) -> Result<usize, String> {
    let text = fs::read_to_string(index_path).map_err(|e| format!("{}: {e}", index_path.display()))?;
    let idx: Index = serde_json::from_str(&text).map_err(|e| format!("index.json: {e}"))?;
    if idx.format != FORMAT {
        return Err(format!("index format {} (this check knows {FORMAT})", idx.format));
    }
    let tmp = std::env::temp_dir().join(format!("katalogos-check-{}", std::process::id()));
    fs::create_dir_all(&tmp).map_err(|e| e.to_string())?;
    let mut seen = std::collections::HashSet::new();
    let mut failed = 0;
    for e in &idx.mods {
        let r = (|| -> Result<ModJson, String> {
            if !valid_repo(&e.repo) {
                return Err("not owner/name".into());
            }
            if !seen.insert(e.repo.to_lowercase()) {
                return Err("listed twice".into());
            }
            if let Some(v) = &e.version {
                version_of(v).ok_or("the pinned version is not x.y.z")?;
            }
            let j: ModJson = serde_json::from_slice(&fetch(&release_url(e, "mod.json"), 1 << 20)?)
                .map_err(|err| format!("mod.json: {err}"))?;
            if j.repo.to_lowercase() != e.repo.to_lowercase() {
                return Err(format!("its mod.json names another repo ({})", j.repo));
            }
            let bw = fetch(&release_url(e, PACKAGE), eidos_mod::package::MAX_TOTAL)?;
            verify(&j, &bw, &tmp)?;
            Ok(j)
        })();
        match r {
            Ok(j) => println!("ok   {} {} ({}){}", e.repo, j.version, j.name, if e.official { " [official]" } else { "" }),
            Err(err) => {
                failed += 1;
                println!("FAIL {}: {err}", e.repo);
            }
        }
    }
    let _ = fs::remove_dir_all(&tmp);
    if failed > 0 {
        Err(format!("{failed} of {} mods failed", idx.mods.len()))
    } else {
        Ok(idx.mods.len())
    }
}

fn arg(args: &[String], name: &str) -> Option<String> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).cloned()
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();
    let r = match args.get(1).map(String::as_str) {
        Some("pack") if args.len() >= 3 => {
            let dir = PathBuf::from(&args[2]);
            let repo = arg(&args, "--repo").unwrap_or_default();
            let version = arg(&args, "--version").unwrap_or_default();
            let out = PathBuf::from(arg(&args, "--out").unwrap_or_else(|| "dist".into()));
            pack(&dir, &repo, &version, &out).map(|j| {
                println!("{} {}: {} files, {} bytes, sha256 {}", j.name, j.version, j.package.files, j.package.size, j.package.sha256);
            })
        }
        Some("check") if args.len() >= 3 => check(Path::new(&args[2])).map(|n| println!("all {n} mods ok")),
        Some("inspect") if args.len() >= 4 => (|| {
            let j: ModJson = serde_json::from_slice(&fs::read(&args[2]).map_err(|e| e.to_string())?)
                .map_err(|e| format!("mod.json: {e}"))?;
            let bw = fs::read(&args[3]).map_err(|e| e.to_string())?;
            let tmp = std::env::temp_dir();
            verify(&j, &bw, &tmp).map(|_| println!("ok {} {}", j.name, j.version))
        })(),
        _ => Err("usage: katalogos pack <mod-dir> --repo owner/name --version x.y.z [--out dir]\n       katalogos check <index.json>\n       katalogos inspect <mod.json> <mod.bw>".into()),
    };
    match r {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("katalogos: {e}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample(dir: &Path, extra: &str) {
        fs::create_dir_all(dir.join("rules")).unwrap();
        fs::write(
            dir.join("eidos_mod.txt"),
            format!("name = Faint Penalty\ndescription = Lose 1000 money when one of your Pokemon faints.\nrule = battle.faint rules/faint_penalty.rule\n{extra}"),
        )
        .unwrap();
        fs::write(dir.join("rules/faint_penalty.rule"), "language 1\nrule faint_penalty on battle.faint\n    when event.side == 0\n    do sub money 1000\n").unwrap();
    }

    #[test]
    fn packs_and_verifies() {
        let t = tempfile::tempdir().unwrap();
        let m = t.path().join("mod");
        sample(&m, "tags = Challenge, rules\nversion = 1.2.0\n");
        let out = t.path().join("dist");
        let j = pack(&m, "Project-Holon/faint-penalty", "v1.2.0", &out).unwrap();
        assert_eq!(j.id, "project-holon/faint-penalty");
        assert_eq!(j.version, "1.2.0");
        assert_eq!(j.author, "Project-Holon");
        assert_eq!(j.games, vec!["black", "white"]);
        assert_eq!(j.tags, vec!["challenge", "rules"]);
        assert_eq!(j.rules, 1);
        assert!(!j.has_code);
        let back: ModJson = serde_json::from_str(&fs::read_to_string(out.join("mod.json")).unwrap()).unwrap();
        assert_eq!(back, j);
        let bw = fs::read(out.join("mod.bw")).unwrap();
        verify(&back, &bw, t.path()).unwrap();
        // a changed byte is caught
        let mut bad = bw.clone();
        let n = bad.len() / 2;
        bad[n] ^= 0xFF;
        assert!(verify(&back, &bad, t.path()).is_err());
        // packing twice gives the same bytes
        let out2 = t.path().join("dist2");
        let j2 = pack(&m, "Project-Holon/faint-penalty", "1.2.0", &out2).unwrap();
        assert_eq!(j2.package.sha256, j.package.sha256);
    }

    #[test]
    fn refuses_mismatches() {
        let t = tempfile::tempdir().unwrap();
        let m = t.path().join("mod");
        sample(&m, "version = 1.0.0\n");
        assert!(pack(&m, "Project-Holon/x", "1.0.1", &t.path().join("o")).is_err());
        assert!(pack(&m, "not-a-repo", "1.0.0", &t.path().join("o")).is_err());
        assert!(pack(&m, "Project-Holon/x", "1.0", &t.path().join("o")).is_err());
        let w = t.path().join("white");
        sample(&w, "game = IRAO\n");
        assert_eq!(pack(&w, "a/b", "1.0.0", &t.path().join("o2")).unwrap().games, vec!["white"]);
        let bad = t.path().join("bad");
        sample(&bad, "game = CPUE\n");
        assert!(pack(&bad, "a/b", "1.0.0", &t.path().join("o3")).is_err());
    }

    #[test]
    fn urls_and_names() {
        assert!(valid_repo("someone/eidos-mod.x_1"));
        assert!(!valid_repo("someone"));
        assert!(!valid_repo("a/b/c"));
        assert!(!valid_repo("a/.."));
        let e = Entry { repo: "a/b".into(), official: false, version: None };
        assert_eq!(release_url(&e, "mod.json"), "https://github.com/a/b/releases/latest/download/mod.json");
        let p = Entry { repo: "a/b".into(), official: true, version: Some("v1.0.0".into()) };
        assert_eq!(release_url(&p, "mod.bw"), "https://github.com/a/b/releases/download/v1.0.0/mod.bw");
    }
}

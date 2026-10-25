#!/usr/bin/env python3
"""Import a local Saraga Hindustani 1.5 download without copying its media.

Uses only the Python standard library. Generated labels deliberately leave
automatic tonic estimates unchecked. Source files are never modified.
"""

import argparse
import collections
import csv
import json
import math
from pathlib import Path
import re
import shutil
import sys
import tempfile
import uuid


# Spelling differences only. In particular, do not collapse Mishra or compound
# raags into their base raag just to make the benchmark larger.
SPELLINGS = {
    "bhairabi": "Bhairavi",
    "jogia": "Jogiya",
    "miyamalhar": "Miyan ki Malhar",
}
# Project decisions for this Saraga import, separate from spelling aliases.
# Preserve the original name and the decision in each imported record.
DATASET_RAAG_MAPPINGS = {"poorva": "Puriya Kalyan"}
EXCLUDED_RAAGS = {"sawani"}
LABEL_COLUMNS = ["file", "raag", "sa_hz", "sa_checked", "artist", "notes",
                 "recording_mbid", "artist_mbid", "raag_uuid", "source_raag",
                 "raag_source", "raag_mapping", "sa_source", "pitch_source"]
ANNOTATIONS = ["ctonic.txt", "pitch.txt", "mphrases-manual.txt",
               "sections-manual-p.txt", "sama-manual.txt", "tempo-manual.txt",
               "bpm-manual.txt"]


def normalize(name):
    return "".join(c for c in name.casefold() if c not in " _-")


def catalog_name(name):
    key = normalize(name)
    return DATASET_RAAG_MAPPINGS.get(key, SPELLINGS.get(key, name))


def read_catalog(path):
    catalog = {}
    with Path(path).open(encoding="utf-8") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if not row or not row[0]:
                raise ValueError("Empty entry in raag catalog")
            for name in row:
                key = normalize(name)
                if key in catalog and catalog[key] != row[0]:
                    raise ValueError(f"Ambiguous raag catalog alias: {name}")
                catalog[key] = row[0]
    if not catalog:
        raise ValueError("The raag catalog is empty")
    return catalog


def source_file(root, relative):
    """Reject absolute paths, traversal and symlinks outside the source tree."""
    root = root.resolve()
    if Path(relative).is_absolute() or ".." in Path(relative).parts:
        raise ValueError(f"Source path escapes dataset root: {relative}")
    path = (root / relative).resolve()
    try:
        path.relative_to(root)
    except ValueError:
        raise ValueError(f"Source path escapes dataset root: {relative}") from None
    if not path.is_file():
        raise ValueError(f"Missing source file: {path}")
    return path


def artist_identity(metadata):
    artists = metadata.get("artists", [])
    candidates = [(a["artist"], "lead") for a in artists if a.get("lead")]
    if not candidates:
        candidates = [(a["artist"], "voice") for a in artists
                      if a.get("instrument", {}).get("name", "").casefold() == "voice"]
    if not candidates:
        candidates = [(a, "album_artist") for a in metadata.get("album_artists", [])]
    unique = {a["mbid"]: (a, origin) for a, origin in candidates if a.get("mbid") and a.get("name")}
    if len(unique) != 1:
        return "", "", "unknown_or_multiple"
    artist, origin = next(iter(unique.values()))
    return artist["name"], artist["mbid"], origin


def raag_identity(metadata, catalog):
    """Use editorial metadata first, then trust a raag named by the title.

    Match the entire name, preserving variants and compound raags. A title
    explicitly starting with Raag/Raga retains its label even when our
    catalog does not yet support it. Do not mine introductions for raag names
    or override a multi-raag metadata entry with a single title label.
    """
    raags = metadata.get("raags", [])
    if len(raags) > 1:
        return "", "", "metadata", ["multiple_raags"]
    if raags:
        name, origin = raags[0]["common_name"], "metadata"
    else:
        title = metadata["title"].strip()
        name = re.sub(r"^(?:raag|raga|rāga)\s+", "", title, flags=re.IGNORECASE)
        lookup = catalog_name(name)
        if name == title and normalize(lookup) not in catalog and normalize(name) not in EXCLUDED_RAAGS:
            return "", "", "unknown", ["missing_raag"]
        origin = "title"
    lookup = catalog_name(name)
    canonical = catalog.get(normalize(lookup), "")
    if normalize(name) in EXCLUDED_RAAGS:
        return name, canonical, origin, ["excluded_raag"]
    return name, canonical, origin, [] if canonical else ["unsupported_raag"]


def inspect(root, catalog):
    root = root.resolve()
    records = []
    seen = set()
    artist_names = {}
    index = source_file(root, "file_paths.csv")
    with index.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError("file_paths.csv contains no recordings")
    for entry in rows:
        mbid = str(uuid.UUID(entry["mbid"]))
        if mbid in seen:
            raise ValueError(f"Duplicate recording MBID: {mbid}")
        seen.add(mbid)
        base = entry["filepath"]
        metadata_path = source_file(root, base + ".json")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if metadata["mbid"] != mbid:
            raise ValueError(f"Index and metadata MBID disagree: {base}")
        audio = source_file(root, base + ".mp3.mp3")
        tonic_path = source_file(root, base + ".ctonic.txt")
        pitch_path = source_file(root, base + ".pitch.txt")
        tonic = float(tonic_path.read_text(encoding="utf-8").strip())
        if not math.isfinite(tonic) or tonic <= 0:
            raise ValueError(f"Invalid tonic: {tonic_path}")
        duration = metadata["length"] / 1000
        if not math.isfinite(duration) or duration <= 0:
            raise ValueError(f"Invalid metadata duration: {base}")
        artist, artist_mbid, artist_source = artist_identity(metadata)
        if artist_mbid:
            # One spelling per stable identity, including album-level fallbacks.
            artist = artist_names.setdefault(artist_mbid, artist)
        raags = metadata.get("raags", [])
        source_raag, canonical, raag_source, reasons = raag_identity(metadata, catalog)
        if not artist_mbid:
            reasons.append("missing_or_multiple_artists")
        annotations = {}
        for extension in ANNOTATIONS:
            candidate = root / (base + "." + extension)
            if candidate.exists():
                annotations[extension] = str(source_file(root, base + "." + extension))
        records.append({
            "recording_mbid": mbid, "title": metadata["title"],
            "file": mbid + ".mp3", "source_path": base,
            "audio_path": str(audio), "metadata_path": str(metadata_path),
            "pitch_path": str(pitch_path), "duration_seconds": duration,
            "audio_bytes": audio.stat().st_size,
            "source_raag": source_raag, "raag": canonical,
            "raag_source": raag_source,
            "raag_mapping": (f"project_decision: {source_raag} -> {canonical}"
                             if canonical and normalize(source_raag) in DATASET_RAAG_MAPPINGS else ""),
            "raag_uuid": raags[0].get("uuid", "") if len(raags) == 1 else "",
            "artist": artist, "artist_mbid": artist_mbid,
            "artist_source": artist_source, "sa_hz": tonic,
            "sa_source": "saraga_automatic_tonic", "sa_checked": False,
            "pitch_source": "saraga_melodia_automatic",
            "annotations": annotations, "eligible": not reasons,
            "exclusion_reasons": reasons,
        })
    # Do not silently miss recordings omitted from the supplied index.
    indexed = {Path(r["metadata_path"]) for r in records}
    actual = {p.resolve() for p in root.rglob("*.json") if not p.name.startswith("._")}
    if indexed != actual:
        raise ValueError("file_paths.csv and metadata JSON file inventory disagree")
    return sorted(records, key=lambda r: r["recording_mbid"])


def summarize(records):
    eligible = [r for r in records if r["eligible"]]
    raag_artists = collections.defaultdict(set)
    for record in eligible:
        raag_artists[record["raag"]].add(record["artist_mbid"])
    return {
        "recordings": len(records),
        "audio_hours": sum(r["duration_seconds"] for r in records) / 3600,
        "eligible_recordings": len(eligible),
        "eligible_hours": sum(r["duration_seconds"] for r in eligible) / 3600,
        "eligible_raags": len(raag_artists),
        "eligible_artists": len({r["artist_mbid"] for r in eligible}),
        "excluded_recordings": len(records) - len(eligible),
        "title_labeled_recordings": sum(r["raag_source"] == "title" for r in records),
        "eligible_title_labeled_recordings": sum(r["raag_source"] == "title" for r in eligible),
        "exclusions": dict(collections.Counter(reason for r in records for reason in r["exclusion_reasons"])),
        "unsupported_raags": dict(collections.Counter(r["source_raag"] for r in records if "unsupported_raag" in r["exclusion_reasons"])),
        "excluded_raags": dict(collections.Counter(r["source_raag"] for r in records if "excluded_raag" in r["exclusion_reasons"])),
        "recordings_by_artist": dict(sorted(collections.Counter(r["artist"] for r in eligible).items())),
        "recordings_by_raag": dict(sorted(collections.Counter(r["raag"] for r in eligible).items())),
        "raags_with_one_artist": sorted(raag for raag, artists in raag_artists.items() if len(artists) == 1),
    }


def label_row(record):
    return {**{k: record[k] for k in LABEL_COLUMNS if k in record},
            "raag": record["raag"] or record["source_raag"] or "Unknown",
            "sa_checked": "no",
            "notes": f'Saraga 1.5; {record["title"]}; artist source: {record["artist_source"]}; automatic Sa and pitch, not human-checked'}


def write_import(root, destination, records):
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Destination already exists; use a new directory to preserve any label edits: {destination}")
    # Construct beside the destination and rename only when the whole import
    # succeeds. Failed imports never leave an apparently complete dataset.
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".saraga-import-", dir=destination.parent))
    try:
        for name in ["collection", "recordings", "pitch", "metadata", "annotations"]:
            (staging / name).mkdir()
        for name, subset in [("collection", records), ("recordings", [r for r in records if r["eligible"]])]:
            with (staging / name / "labels.csv").open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=LABEL_COLUMNS)
                writer.writeheader()
                for record in subset:
                    writer.writerow(label_row(record))
                    (staging / name / record["file"]).symlink_to(record["audio_path"])
        for record in records:
            mbid = record["recording_mbid"]
            (staging / "metadata" / (mbid + ".json")).symlink_to(record["metadata_path"])
            annotation_dir = staging / "annotations" / mbid
            annotation_dir.mkdir()
            for extension, path in record["annotations"].items():
                (annotation_dir / extension).symlink_to(path)
            if record["eligible"]:
                (staging / "pitch" / (record["file"] + ".tsv")).symlink_to(record["pitch_path"])
        manifest = {"dataset": "Saraga Hindustani", "version": "1.5", "source_root": str(root),
                    "raag_label_policy": "metadata_then_trusted_title",
                    "dataset_raag_mappings": DATASET_RAAG_MAPPINGS,
                    "excluded_raags": sorted(EXCLUDED_RAAGS),
                    "dataset_url": "https://zenodo.org/records/4301737",
                    "documentation_url": "https://mtg.github.io/saraga/organization.html",
                    "license": "CC BY-NC-SA 4.0", "summary": summarize(records), "records": records}
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        with (staging / "excluded.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["recording_mbid", "title", "source_raag", "reason"])
            for record in records:
                if not record["eligible"]:
                    writer.writerow([record["recording_mbid"], record["title"], record["source_raag"], "; ".join(record["exclusion_reasons"])])
        staging.rename(destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Extracted saraga1.5_hindustani directory")
    parser.add_argument("--catalog", type=Path, required=True, help="TSV emitted by //tools:raag_catalog")
    parser.add_argument("--output", type=Path, default=Path("data/saraga"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        root = args.source.resolve()
        records = inspect(root, read_catalog(args.catalog))
        if not args.dry_run:
            destination = args.output.absolute()
            # Imports must never create files inside the source download.
            if destination == root or root in destination.resolve().parents:
                raise ValueError("Output must be outside the source dataset")
            write_import(root, destination, records)
            print(f"Imported into {destination}")
        print(json.dumps(summarize(records), indent=2, ensure_ascii=False))
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f"Saraga import failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

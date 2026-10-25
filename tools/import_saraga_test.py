"""Small synthetic datasets exercise import boundaries; no Saraga data needed."""

import csv
import json
from pathlib import Path
import tempfile
import unittest
import uuid

import import_saraga as saraga


class SaragaImportTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "download"
        self.root.mkdir()
        self.destination = self.root.parent / "imported"
        self.index = []
        self.artist_mbid = str(uuid.uuid4())
        self.catalog = {"bhairavi": "Bhairavi", "yaman": "Yaman", "miyankimalhar": "Miyan ki Malhar"}

    def record(self, name="A, quoted recording", raag="Bhairabi", *, lead=False):
        mbid = str(uuid.uuid4())
        relative = f"Album by Artist/{name}/{name}"
        base = self.root / relative
        base.parent.mkdir(parents=True)
        metadata = {
            "mbid": mbid, "title": name, "length": 10000,
            "artists": [{"artist": {"name": "Artist", "mbid": self.artist_mbid},
                         "instrument": {"name": "Voice"}, "lead": lead}],
            "album_artists": [{"name": "Artist", "mbid": self.artist_mbid}],
            "raags": [{"common_name": raag, "uuid": "raag-id"}] if raag else [],
        }
        Path(str(base) + ".json").write_text(json.dumps(metadata), encoding="utf-8")
        Path(str(base) + ".mp3.mp3").write_bytes(b"original media fixture")
        Path(str(base) + ".ctonic.txt").write_text("220.5\n", encoding="utf-8")
        Path(str(base) + ".pitch.txt").write_text("0\t0\n0.004444444\t220.5\n", encoding="utf-8")
        Path(str(base) + ".mphrases-manual.txt").write_text("1\t0\t2\tSRS\n", encoding="utf-8")
        self.index.append({"filepath": relative, "mbid": mbid})
        self.write_index()
        return base

    def write_index(self):
        with (self.root / "file_paths.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["filepath", "mbid"])
            writer.writeheader()
            writer.writerows(self.index)

    def test_import_preserves_sources_and_automatic_provenance_and_explains_exclusions(self):
        self.record()
        self.record("Speech", "")
        self.record("Mishra Piloo", "Mishra piloo")
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        records = saraga.inspect(self.root, self.catalog)
        saraga.write_import(self.root, self.destination, records)
        summary = saraga.summarize(records)
        self.assertEqual(summary["recordings"], 3)
        self.assertEqual(summary["eligible_recordings"], 1)
        self.assertEqual(summary["exclusions"], {"missing_raag": 1, "unsupported_raag": 1})
        self.assertEqual(summary["raags_with_one_artist"], ["Bhairavi"])
        with (self.destination / "recordings/labels.csv").open(newline="", encoding="utf-8") as stream:
            labels = list(csv.DictReader(stream))
        self.assertEqual(len(labels), 1)
        label = labels[0]
        self.assertEqual(label["raag"], "Bhairavi")
        self.assertEqual(label["raag_source"], "metadata")
        self.assertEqual(label["sa_checked"], "no")
        self.assertEqual(label["sa_source"], "saraga_automatic_tonic")
        self.assertEqual(label["artist"], "Artist")
        self.assertEqual(label["artist_mbid"], self.artist_mbid)
        self.assertEqual(float(label["sa_hz"]), 220.5)
        self.assertIn("A, quoted recording", label["notes"])
        audio = self.destination / "recordings" / label["file"]
        self.assertTrue(audio.is_symlink())
        self.assertEqual(audio.read_bytes(), b"original media fixture")
        pitch = self.destination / "pitch" / (label["file"] + ".tsv")
        self.assertTrue(pitch.is_symlink())
        manifest = json.loads((self.destination / "manifest.json").read_text())
        self.assertEqual(len(manifest["records"]), 3)
        self.assertTrue(all(r["artist_source"] == "voice" for r in records))
        self.assertEqual(len(list((self.destination / "collection").glob("*.mp3"))), 3)
        self.assertEqual(before, {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob("*") if p.is_file()})
        # Re-import cannot overwrite locally corrected labels.
        with self.assertRaisesRegex(ValueError, "already exists"):
            saraga.write_import(self.root, self.destination, records)

    def test_uses_catalog_aliases_and_only_explicit_extra_spellings(self):
        catalog = self.root.parent / "catalog.tsv"
        catalog.write_text("Yaman\tKalyan\nBhairavi\nMiyan ki Malhar\tMiyan Malhar\n", encoding="utf-8")
        self.record("Kalyan", "Kalyan", lead=True)
        self.record("Malhar", "Miya malhar")
        records = saraga.inspect(self.root, saraga.read_catalog(catalog))
        self.assertEqual({r["raag"] for r in records}, {"Yaman", "Miyan ki Malhar"})
        self.assertTrue(all(r["eligible"] for r in records))

    def test_rejects_missing_files_mismatched_id_duplicate_id_and_invalid_tonic(self):
        base = self.record()
        tonic = Path(str(base) + ".ctonic.txt")
        for value in ["nan", "-220", "0", "220 Hz"]:
            tonic.write_text(value)
            with self.assertRaises(ValueError):
                saraga.inspect(self.root, self.catalog)
        tonic.write_text("220")
        self.index.append(self.index[0])
        self.write_index()
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            saraga.inspect(self.root, self.catalog)
        self.index.pop()
        self.index[0]["mbid"] = str(uuid.uuid4())
        self.write_index()
        with self.assertRaisesRegex(ValueError, "disagree"):
            saraga.inspect(self.root, self.catalog)
        Path(str(base) + ".mp3.mp3").unlink()
        with self.assertRaises(ValueError):
            saraga.source_file(self.root, self.index[0]["filepath"] + ".mp3.mp3")

    def test_title_labels_do_not_invent_artist_metadata(self):
        base = self.record("Yaman", "")
        path = Path(str(base) + ".json")
        metadata = json.loads(path.read_text())
        metadata["artists"] = []
        metadata["album_artists"] = []
        path.write_text(json.dumps(metadata))
        record = saraga.inspect(self.root, self.catalog)[0]
        self.assertFalse(record["eligible"])
        self.assertEqual(record["exclusion_reasons"], ["missing_or_multiple_artists"])
        self.assertEqual(record["raag"], "Yaman")
        self.assertEqual(record["raag_source"], "title")
        self.assertEqual(record["artist"], "")

    def test_title_fallback_preserves_provenance_and_tonic_status_in_export(self):
        self.record("Raag Yaman", "")
        records = saraga.inspect(self.root, self.catalog)
        record = records[0]
        self.assertTrue(record["eligible"])
        self.assertEqual(record["source_raag"], "Yaman")
        self.assertEqual(record["raag"], "Yaman")
        self.assertEqual(record["raag_source"], "title")
        self.assertEqual(record["raag_uuid"], "")
        self.assertFalse(record["sa_checked"])
        saraga.write_import(self.root, self.destination, records)
        with (self.destination / "recordings/labels.csv").open(newline="", encoding="utf-8") as stream:
            label = next(csv.DictReader(stream))
        self.assertEqual(label["raag_source"], "title")
        self.assertEqual(label["raag"], "Yaman")
        self.assertEqual(label["sa_checked"], "no")
        summary = saraga.summarize(records)
        self.assertEqual(summary["title_labeled_recordings"], 1)
        self.assertEqual(summary["eligible_title_labeled_recordings"], 1)

    def test_title_matching_uses_whole_names_and_keeps_unsupported_labels(self):
        for title in ["Raag Yaman", "raga Kalyan", "Rāga Yaman"]:
            metadata = {"title": title, "raags": []}
            catalog = {**self.catalog, "kalyan": "Yaman"}
            name, canonical, origin, reasons = saraga.raag_identity(metadata, catalog)
            self.assertEqual(canonical, "Yaman")
            self.assertEqual(origin, "title")
            self.assertEqual(reasons, [])
        for title in ["Raag Unknown", "Raag Yaman & Bhairavi", "Raag Yaman Kalyan"]:
            name, canonical, origin, reasons = saraga.raag_identity({"title": title}, self.catalog)
            self.assertEqual(name, title[len("Raag "):])
            self.assertEqual(canonical, "")
            self.assertEqual(origin, "title")
            self.assertEqual(reasons, ["unsupported_raag"])
        for title in ["Introduction about Yaman", "Lyrics of all the Bandishes", "Speech"]:
            self.assertEqual(saraga.raag_identity({"title": title}, self.catalog),
                             ("", "", "unknown", ["missing_raag"]))

    def test_title_does_not_override_existing_or_multiple_raag_metadata(self):
        metadata = {"title": "Raag Yaman", "raags": [{"common_name": "Bhairabi"}]}
        self.assertEqual(saraga.raag_identity(metadata, self.catalog),
                         ("Bhairabi", "Bhairavi", "metadata", []))
        metadata["raags"].append({"common_name": "Yaman"})
        self.assertEqual(saraga.raag_identity(metadata, self.catalog),
                         ("", "", "metadata", ["multiple_raags"]))

    def test_poorva_mapping_preserves_original_title_and_project_provenance(self):
        self.record("Raag Poorva", "")
        catalog = {**self.catalog, "puriyakalyan": "Puriya Kalyan"}
        records = saraga.inspect(self.root, catalog)
        record = records[0]
        self.assertTrue(record["eligible"])
        self.assertEqual(record["source_raag"], "Poorva")
        self.assertEqual(record["title"], "Raag Poorva")
        self.assertEqual(record["raag"], "Puriya Kalyan")
        self.assertEqual(record["raag_source"], "title")
        self.assertEqual(record["raag_mapping"], "project_decision: Poorva -> Puriya Kalyan")
        self.assertEqual(record["raag_uuid"], "")
        self.assertFalse(record["sa_checked"])
        saraga.write_import(self.root, self.destination, records)
        with (self.destination / "recordings/labels.csv").open(newline="", encoding="utf-8") as stream:
            row = next(csv.DictReader(stream))
        self.assertEqual(row["raag"], "Puriya Kalyan")
        self.assertEqual(row["source_raag"], "Poorva")
        self.assertEqual(row["raag_mapping"], record["raag_mapping"])
        # The decision also applies when Saraga supplies the label in metadata.
        self.assertEqual(saraga.raag_identity({"raags": [{"common_name": "Poorva"}]}, catalog),
                         ("Poorva", "Puriya Kalyan", "metadata", []))
        # A missing canonical entry remains unsupported rather than inventing it.
        self.assertEqual(saraga.raag_identity({"title": "Raag Poorva"}, self.catalog)[3],
                         ["unsupported_raag"])

    def test_sawani_stays_excluded_even_if_the_catalog_later_supports_it(self):
        self.record("Raag Sawani", "")
        records = saraga.inspect(self.root, {**self.catalog, "sawani": "Sawani"})
        self.assertFalse(records[0]["eligible"])
        self.assertEqual(records[0]["source_raag"], "Sawani")
        self.assertEqual(records[0]["exclusion_reasons"], ["excluded_raag"])
        self.assertEqual(saraga.summarize(records)["excluded_raags"], {"Sawani": 1})
        saraga.write_import(self.root, self.destination, records)
        self.assertTrue((self.destination / "collection" / records[0]["file"]).exists())
        self.assertFalse((self.destination / "recordings" / records[0]["file"]).exists())

    def test_album_fallback_and_ambiguous_artists(self):
        metadata = {"artists": [], "album_artists": [{"name": "Artist", "mbid": "id"}]}
        self.assertEqual(saraga.artist_identity(metadata), ("Artist", "id", "album_artist"))
        metadata["album_artists"].append({"name": "Other", "mbid": "other-id"})
        self.assertEqual(saraga.artist_identity(metadata), ("", "", "unknown_or_multiple"))

    def test_rejects_index_path_escape_and_outside_symlink(self):
        outside = self.root.parent / "outside.json"
        outside.write_text("{}")
        for relative in ["../outside.json", str(outside)]:
            with self.assertRaisesRegex(ValueError, "escapes"):
                saraga.source_file(self.root, relative)
        (self.root / "escape.json").symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "escapes"):
            saraga.source_file(self.root, "escape.json")

    def test_unindexed_metadata_is_not_silently_omitted(self):
        self.record()
        (self.root / "unindexed.json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "inventory disagree"):
            saraga.inspect(self.root, self.catalog)


if __name__ == "__main__":
    unittest.main()

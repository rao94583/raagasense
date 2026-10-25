import json
from pathlib import Path
import tempfile
import unittest

import score_ablation as analysis


def match(raag, held, missing=0, sa=220):
    components = dict.fromkeys(analysis.COMPONENTS, 0.0)
    components.update(held_fit=held, missing_notes=missing)
    return {"raag": raag, "sa_hz": sa, "score": held + missing, "components": components}


class ScoreAblationTest(unittest.TestCase):
    def test_reconsiders_alternative_sas_when_removing_a_component(self):
        pairs = [match("Other", 0.75), match("Truth", 0.5, sa=330), match("Truth", 1, -3)]
        self.assertFalse(analysis.rank_bounds(pairs, "Truth")["possible_top1"])
        self.assertTrue(analysis.rank_bounds(pairs, "Truth", "missing_notes")["unique_top1"])

    def test_ties_are_bounds_over_raags_not_counts_of_tonic_hypotheses(self):
        pairs = [match("Truth", 1), match("Truth", 1, sa=330), match("Other", 1)]
        bounds = analysis.rank_bounds(pairs, "Truth")
        self.assertFalse(bounds["unique_top1"])
        self.assertTrue(bounds["possible_top1"])
        self.assertTrue(bounds["possible_top3"])
        self.assertFalse(analysis.rank_bounds(pairs, "Absent")["possible_top1"])

    def test_counts_both_gains_and_regressions_against_actual_baseline(self):
        a, b = match("Truth", 1, -2), match("Other", 0)
        c, d = match("Truth", 1), match("Other", 2, -2)
        records = [{"file": "a", "truth_raag": "Truth", "hypotheses": [b, a], "winner": b},
                   {"file": "b", "truth_raag": "Truth", "hypotheses": [c, d], "winner": c}]
        result = analysis.summarize_mode(records, "hypotheses")
        self.assertEqual(result["actual_baseline_top1"], 1)
        variant = result["variants"]["missing_notes"]
        self.assertEqual(variant["unique_top1"], 1)
        self.assertEqual(variant["definite_gains"], 1)
        self.assertEqual(variant["definite_losses"], 1)

    def test_contribution_deltas_explain_the_wrong_pairs_advantage(self):
        winner, reference = match("Other", 0), match("Truth", 1, -2)
        record = {"file": "a.mp3", "truth_raag": "Truth", "hypotheses": [winner, reference],
                  "winner": winner, "winner_raag_correct": False, "reference_pair": reference,
                  "supplied_sa_matches": [reference, winner], "supplied_sa_winner": reference}
        summary, rows = analysis.analyze([record])
        self.assertEqual(summary["largest_advantage_counts"], {"missing_notes": 1})
        self.assertEqual(rows[0]["missing_notes"], 2)
        self.assertEqual(rows[0]["held_fit"], -1)
        self.assertEqual(sum(rows[0][key] for key in analysis.COMPONENTS), rows[0]["score_gap"])

    def test_rejects_incomplete_legacy_and_inconsistent_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            variants = [[], [{"type": "settings", "version": 1}],
                        [{"type": "settings", "version": 2}],
                        [{"type": "settings", "version": 2}, {"type": "complete", "recordings": 1}]]
            for objects in variants:
                path.write_text("".join(json.dumps(o) + "\n" for o in objects))
                with self.assertRaises(ValueError):
                    analysis.read_report(path)
            bad = match("Truth", 1)
            bad["score"] = 2
            objects = [{"type": "settings", "version": 2},
                       {"type": "recording", "file": "a", "hypotheses": [bad], "supplied_sa_matches": []},
                       {"type": "complete", "recordings": 1}]
            path.write_text("".join(json.dumps(o) + "\n" for o in objects))
            with self.assertRaisesRegex(ValueError, "do not sum"):
                analysis.read_report(path)


if __name__ == "__main__":
    unittest.main()

import io
import json
import sqlite3
import tempfile
import threading
import unittest
import urllib.parse
from dataclasses import replace
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image

import geoserver_to_qgc as converter


class _WmsHandler(BaseHTTPRequestHandler):
    transparent = False
    layer_name = "test:layer"

    def do_GET(self):
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        request = query.get("REQUEST", query.get("request", [""]))[0].lower()
        if request == "getcapabilities":
            payload = (
                "<WMT_MS_Capabilities><Capability><Layer><Name>"
                + self.layer_name
                + "</Name></Layer></Capability></WMT_MS_Capabilities>"
            ).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/xml")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        width = int(query["WIDTH"][0])
        height = int(query["HEIGHT"][0])
        color = (0, 0, 0, 0) if self.transparent else (20, 40, 60, 255)
        image = Image.new("RGBA", (width, height), color)
        output = io.BytesIO()
        image.save(output, "PNG")
        payload = output.getvalue()
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format, *args):
        return


class ConverterTests(unittest.TestCase):
    def setUp(self):
        _WmsHandler.transparent = False
        _WmsHandler.layer_name = "test:layer"
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), _WmsHandler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.temp_dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.temp_dir.cleanup()

    def settings(self, output_name="test.qgctiledb"):
        return converter.ConversionSettings(
            wms_url=f"http://127.0.0.1:{self.server.server_port}/wms",
            output=Path(self.temp_dir.name, output_name),
            bounds=converter.GeoBounds(-180, -85.0, 180, 85.0),
            min_zoom=0,
            max_zoom=0,
            layer_rules=(converter.LayerRule(0, 0, ("test:layer",)),),
            map_type=converter.DEFAULT_MAP_TYPE,
            map_type_id=converter.DEFAULT_MAP_TYPE_IDS[converter.DEFAULT_MAP_TYPE],
            set_name="Test set",
            block_tiles=1,
            workers=1,
            timeout=5,
            retries=0,
            wms_format="image/png",
            png_compress_level=6,
            skip_transparent=True,
        )

    def test_plan_and_hash_match_qgc_layout(self):
        settings = self.settings()
        self.assertEqual(converter.plan_rows(settings)[0]["candidate_tiles"], 1)
        self.assertEqual(
            converter.qgc_tile_hash(685018728, 0, 0, 0),
            "06850187280000000000000000000",
        )

    def test_sparse_zoom_areas_limit_high_zoom_blocks(self):
        settings = replace(
            self.settings(),
            max_zoom=1,
            layer_rules=(converter.LayerRule(0, 1, ("test:layer",)),),
            zoom_areas=((1, converter.GeoBounds(-179.0, 1.0, -1.0, 84.0)),),
        )
        rows = converter.plan_rows(settings)
        self.assertEqual(rows[0]["candidate_tiles"], 1)
        self.assertFalse(rows[0]["sparse"])
        self.assertEqual(rows[1]["candidate_tiles"], 1)
        self.assertEqual(rows[1]["blocks"], 1)
        self.assertTrue(rows[1]["sparse"])

    def test_conversion_creates_qgc_database(self):
        _WmsHandler.transparent = False
        settings = self.settings()
        converter.convert(settings, resume=False, max_blocks=None)
        connection = sqlite3.connect(settings.output)
        tile = connection.execute("SELECT hash, format, size, type FROM Tiles").fetchone()
        tile_set = connection.execute("SELECT name, numTiles FROM TileSets").fetchone()
        link_count = connection.execute("SELECT COUNT(*) FROM SetTiles").fetchone()[0]
        connection.close()
        self.assertEqual(tile[0], "06850187280000000000000000000")
        self.assertEqual(tile[1], "png")
        self.assertGreater(tile[2], 0)
        self.assertEqual(tile[3], 685018728)
        self.assertEqual(tile_set, ("Test set", 1))
        self.assertEqual(link_count, 1)
        self.assertFalse(converter.partial_output_path(settings.output).exists())
        manifest_path = converter.manifest_output_path(settings.output)
        self.assertTrue(manifest_path.exists())
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["status"], "complete")
        self.assertEqual(manifest["verification"]["tile_count"], 1)

    def test_transparent_tiles_are_skipped_and_resume_is_idempotent(self):
        _WmsHandler.transparent = True
        settings = self.settings("transparent.qgctiledb")
        converter.convert(settings, resume=False, max_blocks=None)
        converter.convert(settings, resume=True, max_blocks=None)
        connection = sqlite3.connect(settings.output)
        tile_count = connection.execute("SELECT COUNT(*) FROM Tiles").fetchone()[0]
        block_count = connection.execute("SELECT COUNT(*) FROM ConversionBlocks").fetchone()[0]
        connection.close()
        self.assertEqual(tile_count, 0)
        self.assertEqual(block_count, 1)

    def test_partial_file_is_not_published_and_resume_finalizes_it(self):
        settings = replace(
            self.settings("resumed.qgctiledb"),
            max_zoom=1,
            layer_rules=(converter.LayerRule(0, 1, ("test:layer",)),),
        )
        converter.convert(settings, resume=False, max_blocks=1, minimum_free_gib=0)
        partial_path = converter.partial_output_path(settings.output)
        self.assertTrue(partial_path.exists())
        self.assertFalse(settings.output.exists())

        converter.convert(settings, resume=True, max_blocks=None, minimum_free_gib=0)
        self.assertFalse(partial_path.exists())
        self.assertTrue(settings.output.exists())
        self.assertTrue(converter.manifest_output_path(settings.output).exists())

    def test_resume_rejects_changed_geoserver_fingerprint(self):
        settings = replace(
            self.settings("changed-source.qgctiledb"),
            max_zoom=1,
            layer_rules=(converter.LayerRule(0, 1, ("test:layer",)),),
        )
        converter.convert(settings, resume=False, max_blocks=1, minimum_free_gib=0)
        _WmsHandler.layer_name = "test:changed-catalog"
        with self.assertRaisesRegex(RuntimeError, "capabilities changed"):
            converter.convert(settings, resume=True, max_blocks=None, minimum_free_gib=0)

    def test_pause_file_stops_safely_and_progress_reports_resume(self):
        settings = replace(
            self.settings("controlled.qgctiledb"),
            max_zoom=1,
            layer_rules=(converter.LayerRule(0, 1, ("test:layer",)),),
        )
        pause_file = Path(self.temp_dir.name, "controlled.pause")
        progress_file = Path(self.temp_dir.name, "controlled.progress.json")
        pause_file.write_text("pause\n", encoding="utf-8")

        converter.convert(
            settings,
            resume=False,
            max_blocks=None,
            minimum_free_gib=0,
            pause_file=pause_file,
            progress_file=progress_file,
        )
        progress = json.loads(progress_file.read_text(encoding="utf-8"))
        self.assertEqual(progress["status"], "paused")
        self.assertEqual(progress["completed_blocks"], 0)
        self.assertTrue(converter.partial_output_path(settings.output).exists())

        pause_file.unlink()
        converter.convert(
            settings,
            resume=True,
            max_blocks=None,
            minimum_free_gib=0,
            pause_file=pause_file,
            progress_file=progress_file,
        )
        progress = json.loads(progress_file.read_text(encoding="utf-8"))
        self.assertEqual(progress["status"], "complete")
        self.assertEqual(progress["completed_blocks"], 5)


if __name__ == "__main__":
    unittest.main()

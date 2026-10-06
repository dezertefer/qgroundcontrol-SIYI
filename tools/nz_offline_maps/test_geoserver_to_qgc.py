import io
import sqlite3
import tempfile
import threading
import unittest
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image

import geoserver_to_qgc as converter


class _WmsHandler(BaseHTTPRequestHandler):
    transparent = False

    def do_GET(self):
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        request = query.get("REQUEST", query.get("request", [""]))[0].lower()
        if request == "getcapabilities":
            payload = b"<WMT_MS_Capabilities><Capability><Layer><Name>test:layer</Name></Layer></Capability></WMT_MS_Capabilities>"
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


if __name__ == "__main__":
    unittest.main()

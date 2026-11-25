import io
import sqlite3
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

import offline_map_preview as preview


class OfflineMapPreviewTests(unittest.TestCase):
    def test_missing_high_zoom_tile_uses_correct_ancestor_quadrant(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, "sample.qgctiledb")
            connection = sqlite3.connect(path)
            connection.execute(
                "CREATE TABLE TileSets (setID INTEGER, name TEXT, topleftLon REAL, "
                "bottomRightLat REAL, bottomRightLon REAL, topleftLat REAL, "
                "minZoom INTEGER, maxZoom INTEGER)"
            )
            connection.execute(
                "CREATE TABLE Tiles (hash TEXT PRIMARY KEY, tile BLOB, format TEXT)"
            )
            connection.execute(
                "INSERT INTO TileSets VALUES (1, 'Sample', -180, -85, 180, 85, 0, 1)"
            )
            image = Image.new("RGB", (256, 256), "red")
            draw = ImageDraw.Draw(image)
            draw.rectangle((128, 0, 255, 127), fill="green")
            draw.rectangle((0, 128, 127, 255), fill="blue")
            draw.rectangle((128, 128, 255, 255), fill="yellow")
            encoded = io.BytesIO()
            image.save(encoded, "PNG")
            connection.execute(
                "INSERT INTO Tiles VALUES (?, ?, 'png')",
                (preview.qgc_hash(0, 0, 0), encoded.getvalue()),
            )
            connection.commit()
            connection.close()

            collection = preview.OfflineMapCollection(Path(directory))
            try:
                result = collection.tile(1, 1, 0)
                self.assertIsNotNone(result)
                payload, image_format, source = result
                self.assertEqual(image_format, "png")
                self.assertIn("fallback z0", source)
                with Image.open(io.BytesIO(payload)) as fallback:
                    self.assertEqual(fallback.getpixel((128, 128)), (0, 128, 0))
            finally:
                collection.close()


if __name__ == "__main__":
    unittest.main()

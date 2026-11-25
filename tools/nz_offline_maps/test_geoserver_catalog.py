import unittest

import geoserver_catalog as catalog


class GeoServerCatalogTests(unittest.TestCase):
    def test_useful_zoom_is_nearest_native_web_mercator_resolution(self):
        self.assertEqual(catalog.useful_xyz_zoom(2.388657133911758), 16)
        self.assertEqual(catalog.useful_xyz_zoom(1.194328566955879), 17)
        self.assertEqual(catalog.useful_xyz_zoom(0.5971642834779395), 18)

    def test_sparse_areas_are_clipped_and_filtered_by_native_zoom(self):
        package = catalog.Bounds(170.0, -40.0, 175.0, -35.0)
        coverages = [
            catalog.Coverage(
                "fine",
                "Fine chart",
                "group",
                0.5,
                18,
                catalog.Bounds(169.0, -39.0, 172.0, -34.0),
            ),
            catalog.Coverage(
                "coarse",
                "Coarse chart",
                "group",
                2.0,
                16,
                catalog.Bounds(171.0, -39.0, 174.0, -36.0),
            ),
        ]
        areas = catalog.package_zoom_areas(coverages, package)
        self.assertEqual(areas[17], [[170.0, -39.0, 172.0, -35.0]])
        self.assertEqual(areas[18], [[170.0, -39.0, 172.0, -35.0]])
        self.assertNotIn(19, areas)


if __name__ == "__main__":
    unittest.main()

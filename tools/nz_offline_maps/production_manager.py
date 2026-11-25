#!/usr/bin/env python3
"""Desktop controller for the New Zealand QGC offline-map conversion."""

from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import geoserver_catalog


TOOL_DIR = Path(__file__).resolve().parent
CONVERTER = TOOL_DIR / "geoserver_to_qgc.py"
PREVIEWER = TOOL_DIR / "offline_map_preview.py"
PACKAGES_FILE = TOOL_DIR / "new_zealand_packages.json"


def format_size(value: object) -> str:
    try:
        size = int(value)
    except (TypeError, ValueError):
        return "—"
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    amount = float(size)
    for unit in units:
        if amount < 1024.0 or unit == units[-1]:
            return f"{amount:.2f} {unit}" if unit != "B" else f"{int(amount)} B"
        amount /= 1024.0
    return f"{size} B"


class ProductionManager(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("New Zealand QGC Offline Map Production")
        self.geometry("1120x720")
        self.minsize(900, 560)

        definition = json.loads(PACKAGES_FILE.read_text(encoding="utf-8"))
        self.packages = definition["packages"]
        self.jobs = {job["slug"]: job for job in self.packages}
        self.workspace_directory = Path(definition["geoserver_workspace_directory"])
        self.catalog_error: Exception | None = None
        try:
            self.coverages, self.source_plan_sha256 = geoserver_catalog.load_chart_coverages(
                self.workspace_directory
            )
        except Exception as error:
            self.coverages = []
            self.source_plan_sha256 = ""
            self.catalog_error = error
        self.package_plans = {
            str(job["slug"]): self.source_plan(job) for job in self.packages
        }
        self.output_directory = tk.StringVar(
            value=definition["default_output_directory"]
        )
        self.max_hours = tk.StringVar(value="5")
        self.minimum_free_gib = tk.StringVar(value="20")
        self.workers = tk.StringVar(value="32")
        self.status_text = tk.StringVar(value="Ready")
        self.events: queue.Queue[tuple[str, str, object]] = queue.Queue()
        self.pending_slugs: list[str] = []
        self.process: subprocess.Popen[str] | None = None
        self.current_slug: str | None = None
        self.closing_after_pause = False

        self._build_ui()
        self.output_directory.trace_add("write", lambda *_: self.refresh_all())
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.after(250, self.poll_events)
        self.after(1000, self.periodic_refresh)
        self.refresh_all()
        if self.catalog_error is not None:
            self.after(
                100,
                lambda: messagebox.showerror(
                    "GeoServer source audit failed", str(self.catalog_error)
                ),
            )

    def _build_ui(self) -> None:
        root = ttk.Frame(self, padding=10)
        root.pack(fill=tk.BOTH, expand=True)
        root.columnconfigure(1, weight=1)
        root.rowconfigure(3, weight=3)
        root.rowconfigure(6, weight=2)

        ttk.Label(
            root,
            text=(
                "Native-resolution V2 · exact tiger:New Zealand merge composition · "
                "full regional Z13–Z16 + source-aware sparse Z17–Z19"
            ),
        ).grid(row=0, column=0, columnspan=8, sticky="w", pady=(0, 8))

        output_entry = ttk.Entry(root, textvariable=self.output_directory)
        ttk.Label(root, text="Output folder:").grid(row=1, column=0, sticky="w")
        output_entry.grid(row=1, column=1, columnspan=4, sticky="ew", padx=(8, 8))
        ttk.Button(root, text="Browse…", command=self.choose_output).grid(
            row=1, column=5, sticky="ew"
        )
        ttk.Button(root, text="Open Folder", command=self.open_output).grid(
            row=1, column=6, sticky="ew", padx=(8, 0)
        )

        options = ttk.Frame(root)
        options.grid(row=2, column=0, columnspan=7, sticky="ew", pady=(8, 8))
        ttk.Label(options, text="Auto-pause after hours (0 = disabled):").pack(
            side=tk.LEFT
        )
        ttk.Entry(options, textvariable=self.max_hours, width=7).pack(
            side=tk.LEFT, padx=(6, 20)
        )
        ttk.Label(options, text="Keep at least GiB free:").pack(side=tk.LEFT)
        ttk.Entry(options, textvariable=self.minimum_free_gib, width=7).pack(
            side=tk.LEFT, padx=(6, 20)
        )
        ttk.Label(options, text="WMS workers:").pack(side=tk.LEFT)
        ttk.Spinbox(options, from_=1, to=32, textvariable=self.workers, width=5).pack(
            side=tk.LEFT, padx=(6, 20)
        )
        ttk.Label(
            options,
            text="Packages run one at a time to avoid overloading GeoServer.",
        ).pack(side=tk.LEFT)

        columns = ("status", "zooms", "progress", "tiles", "size", "file")
        self.tree = ttk.Treeview(
            root, columns=columns, show="tree headings", selectmode="extended"
        )
        self.tree.heading("#0", text="Package")
        self.tree.heading("status", text="Status")
        self.tree.heading("zooms", text="Planned zooms")
        self.tree.heading("progress", text="Blocks")
        self.tree.heading("tiles", text="Tiles")
        self.tree.heading("size", text="Tile data / file")
        self.tree.heading("file", text="Output")
        self.tree.column("#0", width=210, stretch=False)
        self.tree.column("status", width=100, stretch=False)
        self.tree.column("zooms", width=105, stretch=False)
        self.tree.column("progress", width=130, stretch=False, anchor="e")
        self.tree.column("tiles", width=100, stretch=False, anchor="e")
        self.tree.column("size", width=130, stretch=False, anchor="e")
        self.tree.column("file", width=330)
        self.tree.grid(row=3, column=0, columnspan=7, sticky="nsew")
        scrollbar = ttk.Scrollbar(root, orient=tk.VERTICAL, command=self.tree.yview)
        scrollbar.grid(row=3, column=7, sticky="ns")
        self.tree.configure(yscrollcommand=scrollbar.set)
        for job in self.packages:
            self.tree.insert("", tk.END, iid=job["slug"], text=job["name"])

        buttons = ttk.Frame(root)
        buttons.grid(row=4, column=0, columnspan=7, sticky="ew", pady=(9, 7))
        self.start_selected_button = ttk.Button(
            buttons, text="Start / Resume Selected", command=self.start_selected
        )
        self.start_selected_button.pack(side=tk.LEFT)
        self.start_all_button = ttk.Button(
            buttons, text="Start / Resume All", command=self.start_all
        )
        self.start_all_button.pack(side=tk.LEFT, padx=(8, 0))
        self.pause_button = ttk.Button(
            buttons, text="Pause Safely", command=self.pause, state=tk.DISABLED
        )
        self.pause_button.pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(buttons, text="Refresh", command=self.refresh_all).pack(
            side=tk.LEFT, padx=(8, 0)
        )
        ttk.Button(buttons, text="Check Maps", command=self.open_map_preview).pack(
            side=tk.LEFT, padx=(8, 0)
        )
        ttk.Button(buttons, text="Source Audit", command=self.show_source_audit).pack(
            side=tk.LEFT, padx=(8, 0)
        )

        self.progress = ttk.Progressbar(root, mode="determinate", maximum=100)
        self.progress.grid(row=5, column=0, columnspan=7, sticky="ew")
        ttk.Label(root, textvariable=self.status_text).grid(
            row=5, column=7, sticky="w", padx=(8, 0)
        )

        log_frame = ttk.LabelFrame(root, text="Converter log", padding=5)
        log_frame.grid(row=6, column=0, columnspan=8, sticky="nsew", pady=(9, 0))
        log_frame.rowconfigure(0, weight=1)
        log_frame.columnconfigure(0, weight=1)
        self.log = tk.Text(log_frame, height=10, wrap="word", state=tk.DISABLED)
        self.log.grid(row=0, column=0, sticky="nsew")
        log_scroll = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log.yview)
        log_scroll.grid(row=0, column=1, sticky="ns")
        self.log.configure(yscrollcommand=log_scroll.set)

    def output_dir(self) -> Path:
        return Path(self.output_directory.get()).expanduser().resolve()

    def source_plan(self, job: dict[str, object]) -> dict[str, object]:
        if str(job["kind"]) == "overview":
            return {"max_zoom": 12, "coverage_count": len(self.coverages), "zoom_areas": {}}
        package_bounds = geoserver_catalog.Bounds(*[float(value) for value in job["bounds"]])
        intersecting = [
            coverage
            for coverage in self.coverages
            if coverage.bounds.intersects(package_bounds)
        ]
        zoom_areas = geoserver_catalog.package_zoom_areas(
            intersecting, package_bounds, first_sparse_zoom=17, maximum_zoom=19
        )
        return {
            "max_zoom": max(zoom_areas, default=16),
            "coverage_count": len(intersecting),
            "zoom_areas": zoom_areas,
        }

    def paths(self, slug: str) -> dict[str, Path]:
        directory = self.output_dir()
        output = directory / f"{slug}.qgctiledb"
        return {
            "output": output,
            "partial": output.with_name(output.name + ".partial"),
            "manifest": output.with_name(output.name + ".manifest.json"),
            "progress": output.with_name(output.name + ".progress.json"),
            "pause": output.with_name(output.name + ".pause"),
            "config": directory / "conversion-configs" / f"{slug}.json",
        }

    def package_config(self, job: dict[str, object], output: Path) -> dict[str, object]:
        kind = str(job["kind"])
        if kind == "overview":
            min_zoom, max_zoom = 0, 12
            layer_rules = [
                {
                    "min_zoom": 0,
                    "max_zoom": 12,
                    "layers": ["tiger:New Zealand merge"],
                }
            ]
        else:
            plan = self.package_plans[str(job["slug"])]
            min_zoom, max_zoom = 13, int(plan["max_zoom"])
            layer_rules = [
                {
                    "min_zoom": min_zoom,
                    "max_zoom": max_zoom,
                    "layers": ["tiger:New Zealand merge"],
                }
            ]
        return {
            "wms_url": "http://127.0.0.1:8080/geoserver/wms",
            "output": str(output),
            "bounds": job["bounds"],
            "min_zoom": min_zoom,
            "max_zoom": max_zoom,
            "map_type": "Geoserver Map",
            "map_type_id": 685018728,
            "set_name": f"{job['name']} Native V2",
            "block_tiles": 8,
            "workers": 2,
            "timeout": 120,
            "retries": 3,
            "wms_format": "image/png",
            "png_compress_level": 6,
            "layer_rules": layer_rules,
            "zoom_areas": {
                str(zoom): areas
                for zoom, areas in self.package_plans[str(job["slug"])]["zoom_areas"].items()
            },
            "source_plan_sha256": self.source_plan_sha256,
        }

    def ensure_config(self, slug: str) -> Path:
        if self.catalog_error is not None:
            raise RuntimeError(f"GeoServer source audit failed: {self.catalog_error}")
        job = self.jobs[slug]
        paths = self.paths(slug)
        paths["config"].parent.mkdir(parents=True, exist_ok=True)
        if paths["partial"].exists():
            if not paths["config"].is_file():
                raise RuntimeError(
                    f"{paths['partial'].name} exists but its saved config is missing"
                )
            return paths["config"]
        config = self.package_config(job, paths["output"])
        temporary = paths["config"].with_name(paths["config"].name + ".tmp")
        temporary.write_text(
            json.dumps(config, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        os.replace(temporary, paths["config"])
        return paths["config"]

    def choose_output(self) -> None:
        if self.process is not None:
            messagebox.showinfo("Conversion running", "Pause before changing folders.")
            return
        selected = filedialog.askdirectory(
            title="Choose production output folder", initialdir=self.output_directory.get()
        )
        if selected:
            self.output_directory.set(selected)

    def open_output(self) -> None:
        directory = self.output_dir()
        directory.mkdir(parents=True, exist_ok=True)
        os.startfile(directory)  # type: ignore[attr-defined]

    def open_map_preview(self) -> None:
        directory = self.output_dir()
        if not any(directory.glob("*.qgctiledb")):
            messagebox.showinfo("No completed maps", "No completed map databases were found.")
            return
        creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        subprocess.Popen(
            [sys.executable, str(PREVIEWER), "--directory", str(directory)],
            cwd=TOOL_DIR,
            creationflags=creation_flags,
        )

    def show_source_audit(self) -> None:
        if self.catalog_error is not None:
            messagebox.showerror("Source audit", str(self.catalog_error))
            return
        distribution: dict[int, int] = {}
        for coverage in self.coverages:
            distribution[coverage.useful_max_zoom] = (
                distribution.get(coverage.useful_max_zoom, 0) + 1
            )
        zoom_summary = ", ".join(
            f"Z{zoom}: {count}" for zoom, count in sorted(distribution.items())
        )
        high_detail = sum(
            1 for coverage in self.coverages if coverage.useful_max_zoom >= 17
        )
        messagebox.showinfo(
            "GeoServer native-resolution audit",
            (
                f"Workspace: {self.workspace_directory}\n\n"
                f"Unique chart coverages: {len(self.coverages)}\n"
                f"Coverages useful at Z17–Z19: {high_detail}\n"
                f"Native maximum distribution:\n{zoom_summary}\n\n"
                "Regional packages are complete through Z16. Z17–Z19 are rendered "
                "only over intersecting native chart footprints. Every requested tile "
                "uses tiger:New Zealand merge so overlaps have identical pixels."
            ),
        )

    def selected_slugs(self) -> list[str]:
        selected = set(self.tree.selection())
        return [job["slug"] for job in self.packages if job["slug"] in selected]

    def start_selected(self) -> None:
        slugs = self.selected_slugs()
        if not slugs:
            messagebox.showinfo("Select packages", "Select one or more packages first.")
            return
        self.queue_jobs(slugs)

    def start_all(self) -> None:
        self.queue_jobs([job["slug"] for job in self.packages])

    def queue_jobs(self, slugs: list[str]) -> None:
        try:
            max_hours = float(self.max_hours.get())
            minimum_free = float(self.minimum_free_gib.get())
            workers = int(self.workers.get())
            if max_hours < 0 or minimum_free < 0 or not 1 <= workers <= 32:
                raise ValueError
        except ValueError:
            messagebox.showerror(
                "Invalid options",
                "Hours and free space must be non-negative; workers must be from 1 to 32.",
            )
            return
        for slug in slugs:
            paths = self.paths(slug)
            if paths["output"].is_file():
                self.append_log(f"Skipping completed package: {self.jobs[slug]['name']}\n")
                continue
            if slug != self.current_slug and slug not in self.pending_slugs:
                self.pending_slugs.append(slug)
        self.start_next()

    def start_next(self) -> None:
        if self.process is not None or not self.pending_slugs:
            return
        slug = self.pending_slugs.pop(0)
        paths = self.paths(slug)
        try:
            config_path = self.ensure_config(slug)
            paths["pause"].unlink(missing_ok=True)
            command = [
                sys.executable,
                "-u",
                str(CONVERTER),
                "--config",
                str(config_path),
                "--pause-file",
                str(paths["pause"]),
                "--progress-file",
                str(paths["progress"]),
                "--minimum-free-gib",
                self.minimum_free_gib.get(),
                "--workers",
                self.workers.get(),
            ]
            max_hours = float(self.max_hours.get())
            if max_hours > 0:
                command.extend(("--max-hours", str(max_hours)))
            if paths["partial"].exists():
                command.append("--resume")
            creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
            self.process = subprocess.Popen(
                command,
                cwd=TOOL_DIR,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                creationflags=creation_flags,
            )
        except Exception as error:
            self.process = None
            messagebox.showerror("Could not start conversion", str(error))
            self.pending_slugs.clear()
            return
        self.current_slug = slug
        self.pause_button.configure(state=tk.NORMAL)
        self.start_selected_button.configure(state=tk.DISABLED)
        self.start_all_button.configure(state=tk.DISABLED)
        self.status_text.set(f"Running {self.jobs[slug]['name']}")
        self.append_log(f"\n=== {self.jobs[slug]['name']} ===\n")
        threading.Thread(
            target=self.read_process_output,
            args=(slug, self.process),
            daemon=True,
        ).start()

    def read_process_output(self, slug: str, process: subprocess.Popen[str]) -> None:
        assert process.stdout is not None
        for line in process.stdout:
            self.events.put(("log", slug, line))
        self.events.put(("exit", slug, process.wait()))

    def pause(self) -> None:
        if self.process is None or self.current_slug is None:
            return
        self.pending_slugs.clear()
        pause_path = self.paths(self.current_slug)["pause"]
        pause_path.parent.mkdir(parents=True, exist_ok=True)
        pause_path.write_text("pause after in-flight blocks\n", encoding="utf-8")
        self.pause_button.configure(state=tk.DISABLED)
        self.status_text.set("Pause requested; finishing in-flight blocks…")
        self.append_log("Pause requested. Waiting for in-flight WMS blocks to finish safely.\n")

    def poll_events(self) -> None:
        while True:
            try:
                event, slug, value = self.events.get_nowait()
            except queue.Empty:
                break
            if event == "log":
                self.append_log(str(value))
            elif event == "exit":
                self.handle_exit(slug, int(value))
        self.refresh_all()
        self.after(250, self.poll_events)

    def handle_exit(self, slug: str, exit_code: int) -> None:
        name = self.jobs[slug]["name"]
        progress = self.load_progress(self.paths(slug)["progress"])
        conversion_status = str(progress.get("status", ""))
        self.process = None
        self.current_slug = None
        self.pause_button.configure(state=tk.DISABLED)
        self.start_selected_button.configure(state=tk.NORMAL)
        self.start_all_button.configure(state=tk.NORMAL)
        if exit_code == 0:
            self.append_log(f"=== {name} stopped cleanly ===\n")
            if conversion_status == "complete":
                self.status_text.set("Ready")
            else:
                self.pending_slugs.clear()
                self.status_text.set(
                    str(progress.get("message", "Conversion paused; ready to resume"))
                )
        else:
            self.append_log(f"=== {name} failed with exit code {exit_code} ===\n")
            self.status_text.set(f"Error in {name}")
            self.pending_slugs.clear()
            messagebox.showerror(
                "Conversion stopped",
                f"{name} stopped with an error. Review the converter log.",
            )
        self.refresh_all()
        if self.closing_after_pause:
            self.destroy()
        else:
            self.start_next()

    def load_progress(self, path: Path) -> dict[str, object]:
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}

    def refresh_all(self) -> None:
        active_percent = 0.0
        for job in self.packages:
            slug = job["slug"]
            paths = self.paths(slug)
            progress = self.load_progress(paths["progress"])
            status = str(progress.get("status", "ready")).replace("_", " ").title()
            if paths["output"].is_file():
                status = "Complete"
            elif paths["partial"].is_file() and status in {"Ready", "Starting", "Running"}:
                status = "Partial"
            completed = int(progress.get("completed_blocks", 0) or 0)
            planned = int(progress.get("planned_blocks", 0) or 0)
            blocks = f"{completed:,} / {planned:,}" if planned else "—"
            tiles_value = progress.get("tile_count")
            tiles = f"{int(tiles_value):,}" if tiles_value is not None else "—"
            if paths["output"].is_file():
                size = format_size(paths["output"].stat().st_size)
            else:
                size = format_size(progress.get("tile_bytes"))
            self.tree.item(
                slug,
                values=(
                    status,
                    (
                        "Z0–Z12"
                        if str(job["kind"]) == "overview"
                        else f"Z13–Z{self.package_plans[slug]['max_zoom']}"
                    ),
                    blocks,
                    tiles,
                    size,
                    paths["output"].name,
                ),
            )
            if slug == self.current_slug and planned:
                active_percent = completed * 100.0 / planned
        self.progress["value"] = active_percent

    def periodic_refresh(self) -> None:
        self.refresh_all()
        self.after(1000, self.periodic_refresh)

    def append_log(self, text: str) -> None:
        self.log.configure(state=tk.NORMAL)
        self.log.insert(tk.END, text)
        self.log.see(tk.END)
        self.log.configure(state=tk.DISABLED)

    def on_close(self) -> None:
        if self.process is None:
            self.destroy()
            return
        if messagebox.askyesno(
            "Pause conversion",
            "A conversion is running. Request a safe pause and close after in-flight blocks finish?",
        ):
            self.closing_after_pause = True
            self.pause()


def main() -> int:
    ProductionManager().mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

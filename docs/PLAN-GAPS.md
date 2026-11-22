# Plan: Perbaiki Gap 1, 2, 4 — FuzzyFIS

## Konteks
- Repo: /run/media/ranggawbp/558cd5a9-730a-4d9d-ba58-874c89a20ccf/fuzzy/embedded-fuzzy
- Library: float32, no-malloc, header-only, verified 5 board (CI).
- Oracle: tools/fis2json.py (float64, MATLAB parity) — sudah verified vs Octave.
- check_parity.py jalan di mesin ini: `--ref fis2json` OK, tanpa Octave.

## Tujuan per gap

### Gap #1 — Bisector grid-quantised (error 1e0)
Fakta dari kode (fz_defuzz.c:100-117): binary search di grid 101 titik,
`area_upto` dengan half-step, `lower=mid`. Error = batas kuantisasi grid,
**identik** dengan cara MATLAB menghitung (NumSamplePoints=101). Bukan bug.
Tindakan:
1. Jadikan `FZ_MAX_POINTS` (jumlah titik grid defuzz) **configurable saat
   kompilasi** (default 101, pertahankan parity default).
2. Dokumentasikan di docs/COMPATIBILITY.md §2 bahwa error bisector ∝
   `(hi-lo)/(n_points-1)`; naikkan n_points untuk presisi (biaya RAM linear).
3. Tambah test: biasakan n_points = 201, ukur error bisector turun ~2×
   (≤ setengah grid step), tanpa mengubah fixture default.
Catatan: tak bisa capai 1e-6 tanpa keluar grid referensi — ini merupakan
kesetiaan terhadap definisi MATLAB, bukan kelemahan implementasi.

### Gap #2 — Unit-only operators/MF → golden end-to-end
Fakta: test_ops.c/test_mf.c assert hamacher_*, bounded_sum, algebraic_product,
dsigmf/psigmf/pimf shape — tapi tanpa FIS end-to-end (dokumen §3a menyatakan
ini "weaker form of evidence").
Tindakan:
1. Buat FIS fixture baru di tests/golden/ yang memakai operator/MF tersebut:
   - `ops_hamacher.fis` — AndMethod/OrMethod = hamacher_product/sum.
   - `ops_bounded_sum.fis` — OrMethod = bounded_sum.
   - `ops_algebraic_product.fis` — AndMethod = algebraic_product.
   - `mf_dsigmf.fis`, `mf_psigmf.fis`, `mf_pimf.fis` — pakai MF itu.
2. Bangkit oracle via `check_parity.py --ref fis2json` → simpan `.csv` +
   `.detail.json` + `.in.txt` (grid linspace, ikut pola existing).
3. Daftarkan di `test_regression.c` (atau loop golden) + `make_golden.py`
   CORPUS supaya regenerable.
4. Jalankan & pastikan semua lulus dalam tol 3e-5.

### Gap #4 — CI tidak jalankan parity
Fakta: ci.yml ada host/sanitizer/cpp/codegen/arduino. Belum `check_parity.py`.
Tindakan:
1. Tambah job `parity` di .github/workflows/ci.yml:
   - setup-python, checkout v5 (sudah dipakai).
   - Jalankan `tools/check_parity.py --ref fis2json` atas SEMUA fixture golden
     (`tests/golden/*.fis` + `*.in.txt` pair).
   - Exit non-zero kalau ada yang > tol → fail CI.
2. (Opsional) script pembantu `tools/check_all_parity.sh` untuk loop.

## Urutan eksekusi
1. Gap #4 (ci.yml) — paling cepat, nilai tinggi.
2. Gap #2 (golden baru + regression) — menambah bukti.
3. Gap #1 (configurable n_points + dokumentasi + test) — paling halus.

## Verifikasi akhir
- `make -C tests/host run` → semua lulus.
- `python3 tools/check_parity.py --ref fis2json` atas semua golden → ok.
- commit + push; review (requesting-code-review) sebelum final.

# Dokumentasi Terpadu — opengl_sky

Dokumentasi ini dirancang sebagai satu sumber terpusat untuk: setup, dependensi, build & run, serta tempat menaruh gambar/video dokumentasi. Anda bisa menambahkan screenshot atau video ke folder `docs/images` dan `docs/videos` lalu merujuknya dari file ini.

**Ringkasan singkat**:
- Project C: `opengl_sky` (target CMake)
- Dependency utama yang disertakan: `stb_image.h` (header-only)
- Engine menggunakan OpenGL (GLEW atau build via FetchContent) dan API bawaan OS; ada beberapa file Objective-C++ (`.mm`) untuk macOS.

---

**Prasyarat (Prerequisites)**

- CMake >= 3.24
- Compiler C: Visual Studio (MSVC) on Windows, or GCC/Clang (MinGW/Clang) on Windows; Xcode on macOS untuk build `.mm` Objective-C++ files.
- GPU drivers dengan dukungan OpenGL
- Git (opsional, untuk clone/update)

Catatan: `stb_image.h` sudah ada di repository; tidak perlu instalasi terpisah. GLEW akan dicari oleh CMake dan jika tidak ada, CMake akan men-download/build GLEW via FetchContent.

---

**Build — contoh langkah (Windows)**

Disarankan pakai `cmake` presets jika tersedia (repo menyertakan `CMakePresets.json`). Contoh menggunakan preset:

```bash
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Jika tidak pakai preset, contoh MSVC (Visual Studio):

```powershell
cmake -S . -B build/windows-msvc-debug -G "Visual Studio 17 2022" -A x64
cmake --build build/windows-msvc-debug --config Debug
```

Contoh MinGW/GCC (Makefiles):

```bash
cmake -S . -B build/windows-gcc-debug -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/windows-gcc-debug
```

Contoh Ninja/Clang:

```bash
cmake -S . -B build/windows-clang-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/windows-clang-debug
```

macOS (Xcode):

```bash
cmake -S . -B build/macos-debug -G "Xcode"
cmake --build build/macos-debug --config Debug
```

---

**Menjalankan aplikasi**

Target executable di CMake adalah `opengl_sky`. CMake mengatur agar hasil binary ditempatkan di `CMAKE_BINARY_DIR` (biasanya folder `build/...`). Setelah build selesai, jalankan executable dari folder build yang berisi file `shaders/` dan `res/`.

Contoh (Windows PowerShell):

```powershell
cd build/windows-msvc-debug
./opengl_sky.exe
```

Contoh (Linux/macOS):

```bash
cd build/macos-debug
./opengl_sky
```

Catatan: terdapat perintah post-build yang menyalin folder `shaders` dan `res` ke direktori yang sama dengan executable, jadi pastikan folder tersebut ada di lokasi runtime.

---

**Dependensi & catatan teknis**

- `stb_image.h` — sudah tersedia di repository (header-only). Pastikan salah satu C/CPP file memiliki definisi `#define STB_IMAGE_IMPLEMENTATION` jika diperlukan untuk build lokal.
- OpenGL & GLEW — CMake akan mencari paket OpenGL dan GLEW; jika GLEW tidak ditemukan, CMake akan mendownload sumber GLEW dan membuat target statis.
- vcpkg: repo mencoba auto-detect vcpkg toolchain jika tersedia; ini bersifat opsional.
- macOS: beberapa file (`audio_macos.mm`, `platform_cocoa.mm`) adalah Objective-C++ dan hanya relevan saat membangun di macOS (CMake meng-enable OBJCXX pada Darwin).

---

**Menambahkan gambar & video dokumentasi**

- Buat folder (jika belum): `docs/images` dan `docs/videos`.
- Contoh cara menyisipkan screenshot di Markdown:

```markdown
![Screenshot setup](docs/images/setup.png)
```

- Contoh menyisipkan/menyajikan video:

```html
<video controls src="docs/videos/setup.mp4">Video: <a href="docs/videos/setup.mp4">Download</a></video>
```

- Tips: simpan gambar beresolusi sedang (web-friendly) dan buat thumbnail untuk preview di README jika perlu.

---

**Troubleshooting singkat**

- Jika build gagal karena GLEW/OpenGL: biarkan CMake mendownload GLEW (terkoneksi internet) atau instal paket GLEW/OpenGL di sistem Anda.
- Jika ada error compiler pada Windows saat menggunakan generator "Unix Makefiles" dan compiler MSVC-like: gunakan generator native MSVC (Visual Studio) atau gunakan preset yang sudah disediakan.
- Untuk error macOS terkait Objective-C++: pastikan Xcode command line tools dan SDK terinstal.

---

**Struktur file penting**

- Sumber C utama: `main.c`, `app.c`, dan file-file di root seperti `renderer.c`, `terrain.c`.
- Header utama: `stb_image.h`, `math3d.h`, `renderer.h`, dll.
- Shaders: folder `shaders/` — disalin ke direktori runtime oleh CMake post-build.

---

**Referensi & atribusi**

- LICENSE proyek: [LICENSE](LICENSE)
- Source/inspirasi yang diikuti: https://github.com/shff/opengl_sky.git

_Catatan:_ Saya mengikuti struktur dan sebagian implementasi dari repository di atas saat menyusun proyek ini; referensi lengkap ada di link tersebut.

---

Jika Anda ingin, saya bisa:
- Menambahkan gambar/video yang Anda upload langsung ke `docs/images` / `docs/videos` dan menyisipkannya ke bagian yang sesuai.
- Membuat versi singkat `README.md` yang fokus ke instruksi cepat (one-liner build/run).

Terima kasih — beri tahu file gambar/video yang ingin Anda sertakan atau langsung upload ke repo, saya akan masukkan ke dokumentasi.

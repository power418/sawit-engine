# Sawit Engine

Dokumentasi ini dirancang sebagai satu sumber terpusat untuk: setup, dependensi, build & run, serta tempat menaruh gambar/video dokumentasi. Anda bisa menambahkan screenshot atau video ke folder `docs/images` dan `docs/videos` lalu merujuknya dari file ini.

**Ringkasan singkat**:
- Project C: `opengl_sky` | `sawit-engine` (target CMake)
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

**Build**

Repo menyertakan `CMakePresets.json`. Gunakan preset sesuai platform dan konfigurasi:

Windows (MSVC):

```bash
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Windows (GCC):

```bash
cmake --preset windows-gcc-debug
cmake --build --preset windows-gcc-debug
```

Windows (Clang):

```bash
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
```

Linux (WSL / native):

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
```

macOS:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
```

> Ganti `debug` dengan `release` pada nama preset untuk build Release.

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

**Multiplayer dengan Sawit Service**

Jalankan realtime server dari repo `sawit-service`:

```powershell
cd ..\sawit-service
cargo run --bin sawit-service -- 0.0.0.0:4000
```

Lalu jalankan satu atau dua instance engine. Secara default engine akan connect ke `127.0.0.1:4000`, join room `1`, mengirim input player, menerima snapshot server, dan menggambar remote player sebagai wire box cyan dengan garis arah kuning.

Versi multiplayer terbaru memakai dua tahap:

1. Engine mengirim `JOIN <room_id> <name>` ke TCP control plane `127.0.0.1:4001`.
2. Response `JOIN_OK` memberi `udp_addr`, lalu engine connect ke UDP realtime server tersebut.

Kalau TCP control plane belum tersedia, engine fallback ke UDP address lama dari `SAWIT_SERVICE_ADDR`.

Env opsional sebelum menjalankan engine:

```powershell
$env:SAWIT_CONTROL_ADDR="127.0.0.1:4001"
$env:SAWIT_SERVICE_ADDR="127.0.0.1:4000"
$env:SAWIT_ROOM_ID="1"
$env:SAWIT_PLAYER_NAME="alice"
$env:SAWIT_MULTIPLAYER="1"
```

Untuk skip TCP control plane dan langsung pakai UDP:

```powershell
$env:SAWIT_CONTROL_ADDR="off"
```

Untuk mematikan network client:

```powershell
$env:SAWIT_MULTIPLAYER="0"
```

HUD menampilkan status ringkas: `NET JOIN/TCP` saat sudah discovery via TCP tapi belum welcome UDP, `NET ON/TCP` saat terhubung, `NET ON/UDP` saat fallback direct UDP, `P<id>` untuk local `PlayerId`, `R<n>` untuk jumlah remote player, dan ping terakhir dalam ms.

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


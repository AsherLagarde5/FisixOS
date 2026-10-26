# browsybrowser

A **100% from-scratch native Windows web browser and rendering engine** written in **C++20** and native **Win32**.

**Zero Chromium. Zero WebView2. Zero WebKit. Zero Gecko. Zero Electron.**

---

## Architecture Overview

```
+-----------------------------------------------------------------------------------+
|                        browsybrowser (Native Win32 Shell)                         |
|  [Tabs Strip] [Navigation: Back / Fwd / Reload / Home] [Omnibox] [Bookmarks Bar]  |
+-----------------------------------------------------------------------------------+
                                       |
+-----------------------------------------------------------------------------------+
|                         Render Viewport & Event Handler                           |
|       - Double-buffered GDI / GDI+ Canvas with Anti-Aliasing                      |
|       - Interactive Mouse Hit-Testing (Hyperlinks, Buttons, Inputs, Selection)    |
|       - Smooth Scrolling (Mousewheel, Scrollbar, Arrow/Page Keys)                 |
|       - View Source (Ctrl+U)                                                      |
+-----------------------------------------------------------------------------------+
                                       |
+-----------------------------------------------------------------------------------+
|                        Layout Engine (Box Model & Flow)                           |
|       - LayoutBox Tree (Block, Inline, Inline-Block, None)                        |
|       - Box Model: Dimensions, Margin Collapsing, Padding, Borders, Auto Width    |
|       - Inline Formatting Context: Word-Wrapping & Text Extent Measurement        |
|       - Images, Buttons, Inputs, Lists, Quotes, Preformatted Code Layout          |
+-----------------------------------------------------------------------------------+
                                       |
+-----------------------------------------------------------------------------------+
|                      CSS Parser & Style Cascading Engine                          |
|       - Tokenizer & Rule Parser (Tags, .Classes, #IDs, Attributes, :Hover)        |
|       - Style Resolution: Color, Background, Font, Borders, Radii, Margins, etc.  |
|       - Default User-Agent Stylesheet & Inheritance                               |
+-----------------------------------------------------------------------------------+
                                       |
+-----------------------------------------------------------------------------------+
|                        HTML5 Tokenizer & DOM Tree Parser                          |
|       - Tokenizer: Tags, Attributes, Text Nodes, Comments, Self-Closing Tags      |
|       - DOM Tree Builder: Document, Elements, TextNodes, Tree Traversal           |
|       - Entity Decoder (&nbsp;, &amp;, &lt;, &gt;, &quot;, &#...;)                |
+-----------------------------------------------------------------------------------+
                                       |
+-----------------------------------------------------------------------------------+
|                       Networking & Script Execution Stack                         |
|       - WinHTTP Network Client (HTTP/1.1, HTTPS TLS, Redirects, Headers)          |
|       - Image Asset Cache & GDI+ Image Decoder (PNG, JPEG, GIF, BMP)              |
|       - Internal Schemes (browsy://newtab, browsy://history, browsy://bookmarks)  |
|       - Local Script Runner: <py> (Python stdout) & <script> (Node.js)            |
+-----------------------------------------------------------------------------------+
```

---

## Features

- **100% From-Scratch Rendering Engine**:
  - **HTML Parser** (`engine_html.h`): Parses HTML tags, attributes, void tags, entity codes, and builds a full DOM Tree.
  - **CSS Engine** (`engine_css.h`): CSS tokenizer, selector matching (tags, `.classes`, `#ids`), specificity/source-order cascade, inherited text styles, relative lengths, percentage widths, and alpha colors.
  - **Layout Engine** (`engine_layout.h`): Responsive viewport sizing, box model calculation, padding, borders, inline formatting, and word wrapping using font metrics.
  - **2D Graphics Renderer** (`engine_renderer.h`): GDI+ painting with anti-aliasing, ClearType text rendering, rounded borders, image rendering (`<img>`), form buttons, and interactive links.
  - **Network Engine** (`engine_network.h`): Direct HTTP/HTTPS requests over WinHTTP with TLS certificate validation, redirect following, and binary asset downloading.
  - **Script Engine**: Runs `<py>` tags with local Python and renders output.
- **Native Win32 Chrome & UI**:
  - Multi-tab management (New Tab `+`, Close Tab `×`, Middle-click to close, `Ctrl+T`, `Ctrl+W`, `Ctrl+Shift+T`).
  - Omnibox with URL / search detection and security badge indicator (🔒 HTTPS, ⚠️ HTTP, 🌐 Internal).
  - Bookmarks Bar (`Ctrl+Shift+B`), History (`Ctrl+H`), Bookmarks Manager (`Ctrl+J`).
  - View Page Source (`Ctrl+U`).
  - Dark Mode with Windows 11/10 DWM titlebar integration.
  - Profile persistence in `%LOCALAPPDATA%\browsybrowser\profile.json`.

---

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl + T` | Open New Tab |
| `Ctrl + W` | Close Active Tab |
| `Ctrl + Shift + T` | Reopen Last Closed Tab |
| `Ctrl + Tab` / `Ctrl + PageDown` | Next Tab |
| `Ctrl + Shift + Tab` / `Ctrl + PageUp` | Previous Tab |
| `Ctrl + 1` .. `Ctrl + 8` | Switch to Tab 1–8 |
| `Ctrl + 9` | Switch to Last Tab |
| `Ctrl + L` / `Alt + D` / `F6` | Focus Address Bar & Select All |
| `Ctrl + R` / `F5` | Reload Page |
| `Ctrl + U` | View Page Source |
| `Ctrl + D` | Bookmark Current Page |
| `Ctrl + Shift + B` | Toggle Bookmarks Bar |
| `Ctrl + H` | Open Browsing History (`browsy://history`) |
| `Ctrl + J` | Open Bookmarks (`browsy://bookmarks`) |
| `Alt + Left` | Go Back |
| `Alt + Right` | Go Forward |
| `Alt + Home` | Go to Homepage (`browsy://newtab`) |
| `Up` / `Down` / `PageUp` / `PageDown` | Smooth Scroll Document |

---

## Build

Requires Windows 10/11, Visual Studio 2022 (with Desktop Development with C++), and CMake 3.20+:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The browser executable is produced at:
`build/Release/browsybrowser.exe`

## Android app

The `android/` project is a separate, mobile-first Android browser shell. It uses the platform Android System WebView for web content and provides the same dark navy / teal palette, a compact address bar, a scrollable tab strip, and touch-sized navigation controls. The Windows app continues to use its own from-scratch renderer.

Open `android/` in Android Studio with Android SDK 35 and JDK 17, or build it with Gradle:

```powershell
cd android
gradle :app:assembleDebug
```

The debug APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.


## Share this project? No? fine...

[Share via Email](mailto:?subject=Check%20this%20out&body=Check%20out%20this%20github%20repo!%20https://github.com/asherlagarde/browsybrowser/)

[Twitter](https://twitter.com/intent/tweet?text=Check+out+this+GitHub+repo!+https://github.com/asherlagarde/browsybrowser/)

[Reddit](https://www.reddit.com/submit?url=https://github.com/asherlagarde/browsybrowser/&title=Check+this+out)

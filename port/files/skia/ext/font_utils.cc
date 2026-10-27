// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skia/ext/font_utils.h"

#include "base/check.h"
#include "build/build_config.h"
#include "skia/ext/codec_utils.h"
#include "third_party/skia/include/core/SkFont.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkTypeface.h"

#if BUILDFLAG(IS_ANDROID)
#include <android/api-level.h>

#include "base/feature_list.h"
#include "third_party/skia/include/ports/SkFontMgr_android.h"
#include "third_party/skia/include/ports/SkFontMgr_android_ndk.h"
#include "third_party/skia/include/ports/SkFontScanner_Fontations.h"
#endif

#if BUILDFLAG(IS_APPLE)
#include "third_party/skia/include/ports/SkFontMgr_mac_ct.h"
#endif

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
#include "third_party/skia/include/ports/SkFontConfigInterface.h"
#include "third_party/skia/include/ports/SkFontMgr_FontConfigInterface.h"
#include "third_party/skia/include/ports/SkFontScanner_Fontations.h"
#endif

#if BUILDFLAG(IS_FUCHSIA)
#include <fuchsia/fonts/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "base/fuchsia/process_context.h"
#include "third_party/skia/include/ports/SkFontMgr_fuchsia.h"
#include "third_party/skia/include/ports/SkFontScanner_Fontations.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "third_party/skia/include/ports/SkTypeface_win.h"
#endif

#if defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
#include "third_party/skia/include/ports/SkFontMgr_empty.h"
#endif

#if BUILDFLAG(IS_HAIKU)
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkFontArguments.h"
#include "third_party/skia/include/core/SkFontStyle.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkString.h"
#include "third_party/skia/include/ports/SkFontMgr_directory.h"
#include "third_party/skia/include/ports/SkTypeface_fontations.h"
#endif

#include <mutex>

namespace {

bool g_factory_called = false;

#if BUILDFLAG(IS_HAIKU)
// Haiku has no fontconfig and no font query service; fonts are plain files.
// Skia's directory manager (SkFontMgr_Custom) scans one directory and cannot
// answer "which font has this character?" at all -- its
// onMatchFamilyStyleCharacter() is `return nullptr`, so every glyph outside
// the page's first-choice family rendered as a box. HaikuFontMgr composes
// one directory manager per font location, in priority order, and adds:
//  - per-character fallback across every family, preferring the CJK
//    regional face that matches the page's language (ko -> KR, ja -> JP,
//    zh-Hans -> SC, zh-Hant/zh-HK -> TC/HK), then any font with the glyph;
//  - a sane default family ("Noto Sans", not whatever the scanner met first);
//  - colour emoji. This FreeType is built without COLR and PNG support, so
//    the "Noto Color Emoji" face is reopened through Fontations, which draws
//    COLRv1 (and CBDT), and is preferred whenever Blink asks for emoji
//    presentation (the "und-Zsye" tag).
// Font locations, first wins for equal family names:
//  1. <app dir>/fonts        the bundled Noto Sans CJK (JP/KR/SC/TC/HK),
//                            Noto Sans Arabic and Noto Color Emoji
//  2. /boot/system/data/fonts and the non-packaged/user font directories.
class HaikuFontMgr : public SkFontMgr {
 public:
  explicit HaikuFontMgr(std::vector<sk_sp<SkFontMgr>> parts)
      : parts_(std::move(parts)) {
    bool tried_emoji = false;
    for (const sk_sp<SkFontMgr>& part : parts_) {
      for (int i = 0; i < part->countFamilies(); ++i) {
        SkString name;
        part->getFamilyName(i, &name);
        if (name.equals(kEmojiFamily)) {
          if (!tried_emoji) {
            tried_emoji = true;
            emoji_ = MakeFontationsTypeface(part.get(), i);
          }
          // FreeType would only draw blank or monochrome glyphs from it.
          if (emoji_) {
            continue;
          }
        }
        families_.push_back({part.get(), i, std::string(name.c_str())});
      }
    }
  }

 protected:
  int onCountFamilies() const override {
    return static_cast<int>(families_.size());
  }

  void onGetFamilyName(int index, SkString* family_name) const override {
    family_name->set(families_[index].name.c_str());
  }

  sk_sp<SkFontStyleSet> onCreateStyleSet(int index) const override {
    return families_[index].part->createStyleSet(families_[index].index);
  }

  sk_sp<SkFontStyleSet> onMatchFamily(const char family_name[]) const override {
    if (!family_name) {
      return nullptr;
    }
    if (emoji_ && std::string_view(family_name) == kEmojiFamily) {
      return sk_make_sp<SingleFaceStyleSet>(emoji_);
    }
    for (const sk_sp<SkFontMgr>& part : parts_) {
      sk_sp<SkFontStyleSet> set = part->matchFamily(family_name);
      if (set && set->count() > 0) {
        return set;
      }
    }
    return nullptr;
  }

  sk_sp<SkTypeface> onMatchFamilyStyle(const char family_name[],
                                       const SkFontStyle& style) const override {
    sk_sp<SkFontStyleSet> set = this->onMatchFamily(family_name);
    return set ? set->matchStyle(style) : nullptr;
  }

  sk_sp<SkTypeface> onMatchFamilyStyleCharacter(const char family_name[],
                                                const SkFontStyle& style,
                                                const char* bcp47[],
                                                int bcp47_count,
                                                SkUnichar character) const override {
    // Emoji presentation wants the colour face before anything else.
    if (WantsColorEmoji(bcp47, bcp47_count) && HasGlyph(emoji_, character)) {
      return emoji_;
    }
    // The family the page asked for first, if it happens to have the glyph.
    if (family_name) {
      sk_sp<SkTypeface> tf = this->onMatchFamilyStyle(family_name, style);
      if (HasGlyph(tf, character)) {
        return tf;
      }
    }
    const std::string_view region = PreferredCjkRegion(bcp47, bcp47_count);
    // Pass 1: the CJK face for the page's language. Pass 2: any other CJK
    // face (so zh text on a ko page still renders). Pass 3: everything else.
    for (int pass = 0; pass < 3; ++pass) {
      for (const Family& family : families_) {
        const bool cjk = family.name.find("CJK") != std::string::npos;
        if (pass == 0 && !(cjk && !region.empty() &&
                           family.name.find(region) != std::string::npos)) {
          continue;
        }
        if (pass == 1 && !cjk) {
          continue;
        }
        if (pass == 2 && cjk) {
          continue;
        }
        sk_sp<SkFontStyleSet> set = family.part->createStyleSet(family.index);
        if (!set) {
          continue;
        }
        sk_sp<SkTypeface> tf = set->matchStyle(style);
        if (HasGlyph(tf, character)) {
          return tf;
        }
      }
    }
    // Last resort: an emoji with text presentation that no other font has.
    if (HasGlyph(emoji_, character)) {
      return emoji_;
    }
    return nullptr;
  }

  sk_sp<SkTypeface> onMakeFromData(sk_sp<SkData> data, int ttc_index) const override {
    return parts_.front()->makeFromData(std::move(data), ttc_index);
  }

  sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset> stream,
                                          int ttc_index) const override {
    return parts_.front()->makeFromStream(std::move(stream), ttc_index);
  }

  sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset> stream,
                                         const SkFontArguments& args) const override {
    return parts_.front()->makeFromStream(std::move(stream), args);
  }

  sk_sp<SkTypeface> onMakeFromFile(const char path[], int ttc_index) const override {
    return parts_.front()->makeFromFile(path, ttc_index);
  }

  sk_sp<SkTypeface> onLegacyMakeTypeface(const char family_name[],
                                         SkFontStyle style) const override {
    sk_sp<SkTypeface> tf;
    if (family_name) {
      tf = this->onMatchFamilyStyle(family_name, style);
    }
    if (!tf) {
      tf = this->onMatchFamilyStyle("Noto Sans", style);
    }
    if (!tf) {
      tf = parts_.front()->legacyMakeTypeface(nullptr, style);
    }
    return tf;
  }

 private:
  struct Family {
    SkFontMgr* part;
    int index;
    std::string name;
  };

  // A family with one face, for the Fontations emoji typeface.
  class SingleFaceStyleSet : public SkFontStyleSet {
   public:
    explicit SingleFaceStyleSet(sk_sp<SkTypeface> face)
        : face_(std::move(face)) {}
    int count() override { return 1; }
    void getStyle(int index, SkFontStyle* style, SkString* name) override {
      if (style) {
        *style = face_->fontStyle();
      }
      if (name) {
        name->reset();
      }
    }
    sk_sp<SkTypeface> createTypeface(int index) override { return face_; }
    sk_sp<SkTypeface> matchStyle(const SkFontStyle& pattern) override {
      return face_;
    }

   private:
    sk_sp<SkTypeface> face_;
  };

  static constexpr char kEmojiFamily[] = "Noto Color Emoji";

  static bool HasGlyph(const sk_sp<SkTypeface>& tf, SkUnichar character) {
    return tf && tf->unicharToGlyph(character) != 0;
  }

  // The same font file as the scanner's face, opened by Fontations instead.
  static sk_sp<SkTypeface> MakeFontationsTypeface(SkFontMgr* part, int index) {
    sk_sp<SkFontStyleSet> set = part->createStyleSet(index);
    sk_sp<SkTypeface> scanned = set ? set->matchStyle(SkFontStyle()) : nullptr;
    if (!scanned) {
      return nullptr;
    }
    int ttc_index = 0;
    std::unique_ptr<SkStreamAsset> stream = scanned->openStream(&ttc_index);
    if (!stream) {
      return nullptr;
    }
    return SkTypeface_Make_Fontations(
        std::move(stream), SkFontArguments().setCollectionIndex(ttc_index));
  }

  // Blink adds "und-Zsye" for characters it wants drawn as colour emoji.
  static bool WantsColorEmoji(const char* bcp47[], int count) {
    // SAFETY: Skia's matchFamilyStyleCharacter contract is that `bcp47`
    // points at `count` entries.
    const base::span<const char* const> tags =
        UNSAFE_BUFFERS(base::span(bcp47, static_cast<size_t>(count)));
    for (const char* entry : tags) {
      if (entry && std::string_view(entry) == "und-Zsye") {
        return true;
      }
    }
    return false;
  }

  // Maps the request's BCP 47 tags to the Noto Sans CJK regional face name
  // fragment ("KR", "JP", "SC", "TC", "HK"); empty when no CJK language is
  // among them.
  static std::string_view PreferredCjkRegion(const char* bcp47[], int count) {
    // SAFETY: Skia's matchFamilyStyleCharacter contract is that `bcp47`
    // points at `count` entries.
    const base::span<const char* const> tags =
        UNSAFE_BUFFERS(base::span(bcp47, static_cast<size_t>(count)));
    for (const char* entry : tags) {
      if (!entry) {
        continue;
      }
      const std::string_view tag(entry);
      if (tag.substr(0, 2) == "ko") {
        return "KR";
      }
      if (tag.substr(0, 2) == "ja") {
        return "JP";
      }
      if (tag.substr(0, 2) == "zh") {
        if (tag.find("Hant") != std::string_view::npos ||
            tag.find("TW") != std::string_view::npos) {
          return "TC";
        }
        if (tag.find("HK") != std::string_view::npos ||
            tag.find("MO") != std::string_view::npos) {
          return "HK";
        }
        return "SC";
      }
    }
    return std::string_view();
  }

  std::vector<sk_sp<SkFontMgr>> parts_;
  std::vector<Family> families_;
  sk_sp<SkTypeface> emoji_;
};

sk_sp<SkFontMgr> CreateHaikuFontMgr() {
  std::vector<std::string> dirs;
  base::FilePath app_dir;
  if (base::PathService::Get(base::DIR_MODULE, &app_dir)) {
    dirs.push_back(app_dir.Append("fonts").value());
  }
  dirs.push_back("/boot/system/data/fonts");
  dirs.push_back("/boot/system/non-packaged/data/fonts");
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home)) {
    dirs.push_back(home.Append("config/non-packaged/data/fonts").value());
    dirs.push_back(home.Append("config/data/fonts").value());
  }
  std::vector<sk_sp<SkFontMgr>> parts;
  for (const std::string& dir : dirs) {
    sk_sp<SkFontMgr> part = SkFontMgr_New_Custom_Directory(dir.c_str());
    // A directory with no fonts still yields a manager (with its one
    // placeholder family); keep only the ones that found something real.
    if (part && part->countFamilies() > 0) {
      parts.push_back(std::move(part));
    }
  }
  if (parts.empty()) {
    return SkFontMgr::RefEmpty();
  }
  // The system directory is the one that must answer makeFromStream & co.,
  // but any directory manager will do: they share the FreeType scanner.
  return sk_make_sp<HaikuFontMgr>(std::move(parts));
}
#endif  // BUILDFLAG(IS_HAIKU)

// This is a purposefully leaky pointer that has ownership of the FontMgr.
SkFontMgr* g_fontmgr_override = nullptr;

#if BUILDFLAG(IS_ANDROID)
// https://crbug.com/461659286 Failure to create any font causes crash.
BASE_FEATURE(kUseAndroidNDKFontAPI, base::FEATURE_DISABLED_BY_DEFAULT);
#endif

}  // namespace

namespace skia {

static sk_sp<SkFontMgr> fontmgr_factory() {
  if (g_fontmgr_override) {
    return sk_ref_sp(g_fontmgr_override);
  }

#if BUILDFLAG(IS_ANDROID)
  if (base::FeatureList::IsEnabled(kUseAndroidNDKFontAPI) &&
      android_get_device_api_level() > __ANDROID_API_V__) {
    sk_sp<SkFontMgr> ndk_fontmgr =
        SkFontMgr_New_AndroidNDK(false, SkFontScanner_Make_Fontations());
    if (ndk_fontmgr && ndk_fontmgr->countFamilies()) {
      return ndk_fontmgr;
    }
  }
  return SkFontMgr_New_Android(nullptr, SkFontScanner_Make_Fontations());
#elif BUILDFLAG(IS_APPLE)
  return SkFontMgr_New_CoreText(nullptr);
#elif BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
  sk_sp<SkFontConfigInterface> fci(SkFontConfigInterface::RefGlobal());
  return fci ? SkFontMgr_New_FCI(std::move(fci),
                                 SkFontScanner_Make_Fontations())
             : nullptr;
#elif BUILDFLAG(IS_FUCHSIA)
  fuchsia::fonts::ProviderSyncPtr provider;
  base::ComponentContextForProcess()->svc()->Connect(provider.NewRequest());
  return SkFontMgr_New_Fuchsia(std::move(provider),
                               SkFontScanner_Make_Fontations());
#elif BUILDFLAG(IS_WIN)
  return SkFontMgr_New_DirectWrite();
#elif BUILDFLAG(IS_HAIKU)
  // No fontconfig and no font provider service: the fonts are files under
  // the app's fonts/ directory and the system font directories; see
  // HaikuFontMgr above for the per-character fallback this adds on top of
  // Skia's directory scanner.
  return CreateHaikuFontMgr();
#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
  return SkFontMgr_New_Custom_Empty();
#else
  return SkFontMgr::RefEmpty();
#endif
}

sk_sp<SkFontMgr> DefaultFontMgr() {
  static std::once_flag flag;
  static SkFontMgr* mgr;
  std::call_once(flag, [] {
    mgr = fontmgr_factory().release();
    g_factory_called = true;
  });
  return sk_ref_sp(mgr);
}

void OverrideDefaultSkFontMgr(sk_sp<SkFontMgr> fontmgr) {
  CHECK(!g_factory_called);

  SkSafeUnref(g_fontmgr_override);
  g_fontmgr_override = fontmgr.release();
}

sk_sp<SkTypeface> MakeTypefaceFromName(const char* name, SkFontStyle style) {
  sk_sp<SkFontMgr> fm = DefaultFontMgr();
  CHECK(fm);
  sk_sp<SkTypeface> face = fm->legacyMakeTypeface(name, style);
  return face;
}

sk_sp<SkTypeface> DefaultTypeface() {
  sk_sp<SkTypeface> face = MakeTypefaceFromName(nullptr, SkFontStyle());
  if (face) {
    return face;
  }
  // Due to how SkTypeface::MakeDefault() used to work, many callers of this
  // depend on the returned SkTypeface being non-null. An empty Typeface is
  // non-null, but has no glyphs.
  face = SkTypeface::MakeEmpty();
  CHECK(face);
  return face;
}

SkFont DefaultFont() {
  return SkFont(DefaultTypeface());
}

void InitializeFontRendering() {
  // for Fontations and DirectWrite to process emojis
  skia::EnsurePNGDecoderRegistered();
}

}  // namespace skia

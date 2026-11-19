![logo](docs/images/logo.png)

<p align="center">
  <img src="https://img.shields.io/github/downloads/YasserNull/skia-motion-blur/total?label=Downloads"/>
  <img src="https://img.shields.io/github/v/release/YasserNull/skia-motion-blur?include_prereleases&label=Release"/>
  <img src="https://img.shields.io/badge/License-LGPLv2.1-blue.svg"/>
</p>

# Skia Motion Blur

Magisk Module adds Motion Blur For Android 

---

## ما هي Skia Motion Blur؟

**Skia Motion Blur** وحدة ماجيسك تضيف تأثير ضبابية الحركة (Motion Blur) إلى واجهة النظام والتطبيقات والألعاب، لجعل الحركة أكثر سلاسة بصريًا.

استُوحي الاسم من مكتبة الرسوميات **Skia** المستخدمة في أندرويد. لكن التنفيذ الحالي لا يعترض مكتبة Skia مباشرةً؛ بل يعمل داخل `SurfaceFlinger`.

## How Skia Motion Blur Works

تعمل الوحدة داخل عملية `SurfaceFlinger`، وتعترض مسارات الرسم المدعومة عبر `EGL/OpenGL ES` لتقدير الحركة بين الإطارات وتطبيق ضبابية اتجاهية على خرج تركيب الإطارات المؤهل.

تقوم **Skia Motion Blur** بحقن مكتبة أصلية داخل عملية `SurfaceFlinger` في أندرويد باستخدام `LD_PRELOAD`. وتعترض مسارات الرسم المدعومة عبر `EGL/OpenGL ES`، وتحلل الحركة بين الإطارات المتتالية، ثم تطبّق ضبابية حركة اتجاهية على خرج تركيب الإطارات المؤهل.

لشرح تفصيلي لآلية الحقن وخوارزمية تقدير الحركة ومراحل المعالجة وحدود التوافق، راجع [How Skia Motion Blur Works](docs/how_skia_motion_blur_works.md).

## المميزات

- **حركة أكثر سلاسة بصريًا:** إضافة ضبابية الحركة أثناء التمرير والتنقل بين التطبيقات، وحركات واجهة النظام والألعاب.
- **دعم الألعاب:** تأثير يضهر ف العاب
- **التخصيص:** يمكنك تحديد تطبيقات معينة و تعديل اعدادات تأثير
- **اداء سريع** لا تقوم الوحدة ب إبطاء الهاتف ، كما انها مصممة بلغة c السريعة

## المتطلبات والتوافق

| المتطلب | الدعم |
|---|---|
| إصدار أندرويد | Android 12–16 |
| مستوى API | 31–36 |
| واجهة الرسوميات | OpenGL ES 3 |
| Magisk | الإصدار 26 أو أحدث |
| KernelSU | مدعوم |
| APatch | مدعوم |
| Zygisk | غير مطلوب |

### لماذا Android 12 فما فوق؟

يستهدف التنفيذ الحالي واجهات ومسارات `SurfaceFlinger` و`EGL` ضمن API 31–36. تختلف تفاصيل التنفيذ في الإصدارات الأقدم، لذلك يرفض المثبّت الإصدارات الأقل من Android 12.

### توافق الإصدارات

| الإصدار | استراتيجية EGL |
|---|---|
| Android 12–13 | EGL KHR |
| Android 14–16 | EGL KHR وEGL 1.5 |
| خارج API 31–36 | غير مدعوم |

قد يختلف التوافق الفعلي بين الأجهزة والرومات بحسب تنفيذ `SurfaceFlinger` وتعريفات الرسوميات وآلية تركيب ملفات الوحدة.

## التثبيت

### تحذير قبل التثبيت

تعمل الوحدة داخل عملية `SurfaceFlinger`، وقد تتسبب في حدوث **Bootloop** على بعض الأجهزة أو الرومات غير المتوافقة.

**يُنصح بشدة بتثبيت وحدة Bootloop Protector قبل تثبيت Skia Motion Blur.** قد تساعد في الاسترداد عند حدوث مشكلة،.
### خطوات التثبيت

1. نزّل أحدث ملف ZIP من [صفحة الإصدارات](https://github.com/YasserNull/skia-motion-blur/releases).
2. افتح مدير الروت: Magisk أو KernelSU أو APatch.
3. ثبّت الوحدة من ملف ZIP.
4. أعد تشغيل الجهاز.
5. عدّل ملف إعدادات الوحدة حسب رغبتك.

## الإعدادات

يستخدم المشروع ملف `skia-motion-blur.prop` بصيغة `key=value` داخل مجلد الوحدة.

### الإعدادات الافتراضية

```properties
enabled=1
strength=8.0
exposure_ms=16.667
temp_settings=0
log=0
debug=0
allow_packages=*
history_scale=4
history_timeout_ms=150
memory_limit_mb=48
quality=high
```

### شرح الإعدادات

| المفتاح | الوصف | القيم |
|---|---|---|
| `enabled` | تشغيل التأثير أو إيقافه | `0` أو `1` |
| `strength` | قوة ضبابية الحركة | `0–8` |
| `exposure_ms` | مدة التعريض بالملّي ثانية | `1–50` |
| `temp_settings` | مراقبة تغييرات الإعدادات دوريًا | `0` أو `1` |
| `log` | تفعيل سجلات التشخيص | `0` أو `1` |
| `debug` | عرض معلومات تصحيح التأثير | `0` أو `1` |
| `allow_packages` | التطبيقات المسموح بها | `*` أو أسماء الحزم |
| `history_scale` | دقة الإطارات المستخدمة لتقدير الحركة | `1–4` |
| `history_timeout_ms` | مهلة صلاحية الإطار السابق | `16–250` |
| `memory_limit_mb` | الحد التقديري لذاكرة قوام التأثير لكل سياق رسم | `8–128` |
| `quality` | مستوى الجودة الاختياري | `low` أو `balanced` أو `high` |

### مستويات الجودة

| الجودة | القيمة | دقة التاريخ | استهلاك الموارد |
|---|---|---|---|
| `low` | `history_scale=4` | ربع العرض والارتفاع | أقل |
| `balanced` | `history_scale=2` | نصف العرض والارتفاع | متوسط |
| `high` | `history_scale=1` | الدقة الكاملة | أعلى |

مثال:

```properties
quality=balanced
```

### تحديد التطبيقات

لتفعيل التأثير مع جميع التطبيقات:

```properties
allow_packages=*
```

لتفعيله مع تطبيقات محددة فقط:

```properties
allow_packages=com.android.settings,com.android.launcher3
```

### تحديث الإعدادات

عند ضبط `temp_settings=1`، يعيد المتحكم قراءة ملف الوحدة كل خمس ثوانٍ تقريبًا، وتفحص مكتبة التأثير النسخة الداخلية أثناء الرسم كل ثانية تقريبًا.

عند ضبط `temp_settings=0`، لا يراقب المتحكم التغييرات دوريًا بعد التهيئة. يُختار هذا النمط عند بدء المتحكم، لذلك يتطلب تغيير `temp_settings` نفسه إعادة بدء المتحكم.

### استهلاك الذاكرة

يحدد `memory_limit_mb` الحد التقديري لذاكرة القوام لكل سياق رسم. ويعتمد التقدير على أبعاد الشاشة وقيمة `history_scale`:

لا يشمل هذا التقدير بالضرورة جميع الموارد الداخلية لتعريف الرسوميات.

## التشخيص وحل المشكلات

لتفعيل السجلات:

```properties
log=1
```

لعرض سجلات الوحدة:

```sh
su -c 'logcat -d -s SkiaMotionBlur'
```

## البناء من المصدر

### المتطلبات

- Python 3
- Android NDK
- LLVM / Clang
- CMake لتشغيل الاختبارات

### بناء الوحدة

```sh
python3 tools/build_runtime.py --ndk /path/to/android-ndk
```

تُنشأ الحزمة في:

```text
release/skia-motion-blur-<version>.zip
```

يستخدم المشروع C11 وتحسينات `-O2`، ويستهدف API 31 كحد أدنى. تتضمن الحزمة ملف `binaries.sha256.json` لبصمات الملفات الثنائية.

## الاختبارات

```sh
cmake -S . -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python3 -m unittest discover -s tests
```

تحتاج اختبارات الرسم الفعلية إلى بيئة أندرويد مناسبة.

## إيقاف التأثير وإزالة الوحدة

يمكن إيقاف التأثير من الإعدادات:

```properties
enabled=0
```

لإزالة الوحدة، احذفها من مدير الروت وأعد تشغيل الجهاز.

## المساهمة في المشروع

المساهمات والإبلاغ عن الأخطاء واختبارات التوافق مرحّب بها:

- [الإبلاغ عن مشكلة](https://github.com/YasserNull/skia-motion-blur/issues)
- [طلبات الدمج](https://github.com/YasserNull/skia-motion-blur/pulls)
- [الإصدارات](https://github.com/YasserNull/skia-motion-blur/releases)

## الترخيص

المشروع مرخّص بموجب [GPLv3](LICENSE).

## دعم المشروع

يمكنك دعم التطوير عبر [Ko-fi](https://ko-fi.com/yassernull).

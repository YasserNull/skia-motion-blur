![logo](docs/images/logo.png)

<p align="center">
  <img src="https://img.shields.io/github/downloads/YasserNull/skia-motion-blur/total?label=Downloads"/>
  <img src="https://img.shields.io/github/v/release/YasserNull/skia-motion-blur?include_prereleases&label=Release"/>
  <img src="https://img.shields.io/badge/License-GPLv3-blue.svg"/>
</p>

# Skia Motion Blur

---

## ما هي Skia Motion Blur؟

**Skia Motion Blur** وحدة للنظام تضيف ضبابية الحركة إلى بعض الإطارات المؤهلة أثناء التمرير والانتقالات. يعتمد ظهورها على طريقة رسم المحتوى والجهاز.

لا تضيف الوحدة إطارات جديدة ولا ترفع معدل الإطارات (FPS).

استُوحي اسم المشروع من مكتبة الرسوميات **Skia** المستخدمة في أندرويد. لكن التنفيذ الحالي يعمل داخل `SurfaceFlinger` دون اعتراض مكتبة Skia مباشرة.

## How Skia Motion Blur Works

تعمل الوحدة داخل `SurfaceFlinger` وتقارن صورًا متتالية من مخرج رسم مؤهل لتقدير اتجاه الحركة ومقدارها، ثم تضيف الضبابية إلى ذلك المخرج.

![مقارنة الإطار السابق والحالي لإضافة ضبابية باتجاه الحركة](docs/images/motion-blur.svg)

لمعرفة المزيد عن آلية العمل ومراحل المعالجة راجع [How Skia Motion Blur works](docs/how_skia_motion_blur_works.md).

## المميزات

- ضبابية الحركة أثناء التمرير والانتقالات.
- معالجة رسومية على مستوى النظام.
- تخصيص التأثير ودقة المعالجة وتحديد التطبيقات أثناء التشغيل.

### متى يظهر التأثير؟

وجود تطبيق مسموح به وحده لا يكفي؛ يحتاج التأثير إلى مخرج رسم مؤهل وحركة يمكن تقديرها بين صورتين متتاليتين. اللمس يزيد حساسية التأثير للحركة الخفيفة لفترة قصيرة، لكنه ليس شرطًا لظهوره.

## المتطلبات والتوافق

| المتطلب | الدعم |
|---|---|
| Android | 12–16 |
| Magisk | 26 أو أحدث |

> [!NOTE]
> يدعم المثبّت KernelSU وAPatch أيضًا عند توفر تركيب ملفات الوحدة على مستوى النظام.
>
> Zygisk is not required.

يتطلب التأثير مخرج رسم متوافقًا مع OpenGL ES 3؛ دعم إصدار أندرويد أو مدير الروت وحده لا يضمن ظهوره.

### لماذا Android 12 فما فوق؟

يقتصر الدعم على Android 12–16 وفقًا لسياسة التوافق الخاصة بالوحدة بسبب عدم التحقق من توافق التنفيذ واستقراره على الإصدارات الأقدم.

### توافق الإصدارات

| الإصدار | استراتيجية EGL |
|---|---|
| Android 12–13 | EGL KHR |
| Android 14–16 | EGL KHR وEGL 1.5 |
| خارج Android 12–16 | غير مدعوم |

قد يختلف التوافق حسب تنفيذ `SurfaceFlinger` وتعريفات الرسوميات في الجهاز.

## التثبيت

> [!CAUTION]
> ### تحذير قبل التثبيت
>
> تعمل الوحدة داخل عملية `SurfaceFlinger` وقد تتسبب في حدوث **Bootloop** على الأجهزة أو الرومات غير المتوافقة.
>
> **يُنصح بشدة بتثبيت وحدة Bootloop Protector قبل تثبيت Skia Motion Blur.**

### خطوات التثبيت

1. نزّل أحدث ملف ZIP من [صفحة الإصدارات](https://github.com/YasserNull/skia-motion-blur/releases).
2. افتح مدير الروت.
3. ثبّت الوحدة من ملف ZIP.
4. أعد تشغيل الجهاز.

## الإعدادات

يمكن تخصيص التأثير من خلال ملف `skia-motion-blur.prop` الموجود داخل مجلد الوحدة.

**Magisk وAPatch**

```text
/data/adb/modules/skia-motion-blur/skia-motion-blur.prop
```

**KernelSU**

```text
/data/adb/ksu/modules/skia-motion-blur/skia-motion-blur.prop
```

تُطبّق التعديلات عادة خلال بضع ثوانٍ دون الحاجة إلى إعادة تشغيل الجهاز.

### الإعدادات الافتراضية

```properties
enabled=1
strength=8.0
exposure_ms=8.333
log=0
debug=0
allow_packages=*
history_scale=1
history_timeout_ms=150
memory_limit_mb=128
```

### شرح الإعدادات

| الإعداد | الوصف | القيم |
|---|---|---|
| `enabled` | تشغيل التأثير أو إيقافه | `0` أو `1` |
| `strength` | قوة ضبابية الحركة | `0–8` |
| `exposure_ms` | مدة التعريض بالملّي ثانية | `1–50` |
| `log` | تسجيل معلومات التشخيص | `0` أو `1` |
| `debug` | الوضع التشخيصي وإيقاف طلبات تنظيف الأثر بعد الحركة | `0` أو `1` |
| `allow_packages` | التطبيقات المسموح لها بتشغيل التأثير | `*` أو أسماء الحزم |
| `exclude_packages` | التطبيقات المستثناة من التأثير | أسماء الحزم |
| `history_scale` | دقة تقدير الحركة وحساب الضبابية | `1–4` |
| `history_timeout_ms` | أقصى فاصل زمني بين إطارين لحساب الحركة | `16–250` |
| `memory_limit_mb` | الحد الأقصى لذاكرة صور المعالجة لكل سياق رسم بالميغابايت | `8–128` |

زيادة `strength` تجعل الضبابية أقوى. وزيادة `exposure_ms` قد تجعل أثر الحركة أطول. استخدام `strength=0` يوقف التأثير.

### مستويات `history_scale`

![History Scale](docs/images/history-scale.svg)

| القيمة | دقة العرض والارتفاع | نسبة عدد البكسلات | الأثر المتوقع |
|---|---:|---:|---|
| `1` | 100% | 100% | أعلى تفاصيل وأكبر استهلاك للموارد |
| `2` | 50% | 25% | توازن بين الجودة والأداء |
| `3` | 33.3% | 11.1% | استهلاك منخفض للموارد |
| `4` | 25% | 6.25% | أقل استهلاك متوقع وتفاصيل أقل |

عند استخدام قيمة أعلى من `1` تُحسب الضبابية بدقة أقل ثم يُدمج الفرق الناتج مع الصورة الأصلية بدقة الإخراج.

> [!NOTE]
> لا يغير هذا الإعداد دقة الشاشة. النسب تمثل حجم صور المعالجة وليست قياسات فعلية للأداء.

### تحديد التطبيقات

لتفعيل التأثير مع جميع التطبيقات:

```properties
allow_packages=*
```

لتفعيله مع تطبيقات محددة فقط:

```properties
allow_packages=com.android.settings,com.example.app
```

لاستثناء تطبيقات محددة:

```properties
allow_packages=*
exclude_packages=com.example.app
```

> [!NOTE]
> يحدد اختيار التطبيقات متى يُسمح بتشغيل التأثير. لا يعزل طبقة التطبيق عن بقية عناصر الصورة المركبة.

## السجلات

لتفعيل السجلات:

```properties
log=1
```

لعرض سجلات الوحدة:

```sh
logcat -s SkiaMotionBlur
```

## البناء من المصدر

### المتطلبات

- Python 3
- Android NDK
- LLVM / Clang
- CMake لتشغيل الاختبارات

### بناء الوحدة

```sh
python3 tools/build_runtime.py --ndk /path/to/ndk/version
```

تُنشأ الحزمة في:

```text
release/skia-motion-blur-<version>.zip
```

## الاختبارات

```sh
cmake -S . -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python3 -m unittest discover -s tests
```

> [!NOTE]
> تحتاج اختبارات الرسم الفعلية إلى بيئة أندرويد مناسبة.

## المساهمة في المشروع

المساهمات والإبلاغ عن الأخطاء واختبارات التوافق مرحّب بها:

- [الإبلاغ عن مشكلة](https://github.com/YasserNull/skia-motion-blur/issues)
- [طلبات الدمج](https://github.com/YasserNull/skia-motion-blur/pulls)

## الترخيص

المشروع مرخّص بموجب [GPLv3](LICENSE).

## التبرع

يمكنك دعم المشروع عبر [Ko-fi](https://ko-fi.com/yassernull).

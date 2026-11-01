# MISHPACHA (משפחה)

משחק פשע בעולם פתוח שמתרחש ב**חוף הזהב**, אזור בדיוני בהשראת ישראל, ובמרכזו העיר **תל אויר**.
שלושה אחים ממשפחת סלע, ואפשר להחליף ביניהם בכל רגע:

| דמות | מי | יכולת מיוחדת (מקש Q) |
|---|---|---|
| **רפי** | האח הגדול, מנהל את "העסק" | **"תקרא לחבר'ה"**: חיילי משפחה מגיעים ומלווים אותו |
| **שירה** | עורכת דין פלילית שמנסה לצאת מהעסק | **"קשרים"**: שיחת טלפון אחת מורידה 2 רמות חום (לא עובד ברמה 5) |
| **עומר** | הצעיר, השתחרר עכשיו מהצבא, מפעיל רחפנים לשעבר | **"רחפן"**: מטיסים רחפן שמסמן אויבים |

📄 **מסמך העיצוב המלא (GDD):** https://claude.ai/artifact/LJYuWhDztrHMuYYphr1ahf

---

## ⚠️ מצב נוכחי

- זה **שלד קוד C++**: המערכות המרכזיות כתובות, אבל עדיין אין גרפיקה, מפה או דמויות אמיתיות.
- הקוד **נכתב בלי קימפול**, כי בסביבה שבה נכתב אין Unreal Engine. ייתכן שבבנייה הראשונה יופיעו כמה שגיאות קטנות.
  אם זה קורה, העתק את שגיאות הקומפיילר (חלון Output ב-Visual Studio) ושלח ל-Claude לתיקון.
- נכסים של Unreal (`.uasset`, `.umap`) הם קבצים בינאריים, ולכן אותם יוצרים בעורך לפי השלבים שבהמשך.

## מה כבר קיים בקוד

| מערכת | מה היא עושה |
|---|---|
| **משפחה / החלפת דמויות** | 3 אחים, החלפה ב-1/2/3 עם מעבר "צ'אט משפחתי" (מסך מחשיך → UI → מצלמה חוזרת). אח שלא בשליטה ממשיך לחיות עם AI, ואם הוא רחוק מדי הוא "מוקפא" כדי לחסוך ביצועים. לכל אח ארנק משלו |
| **חום (מבוקשות)** | רמות 0–5. פשעים מוסיפים נקודות, ורק פשע שיש לו עדים נחשב (יריות נשמעות גם בלי עדים). מתחמקים מהשוטרים כדי לרדת ל-0 |
| **משטרה** | מספר יחידות לפי רמת חום, הגעה מדורגת, מרדף אחרי "המיקום האחרון הידוע" |
| **רכבים** | Chaos Vehicles: כניסה ויציאה (F), גניבה וחטיפת רכב מדווחות כפשע, מצלמה חופשית, רדיו (R) |
| **לחימה** | נשק hitscan שהשרת מאמת, בריאות, מוות עם ragdoll, אשפוז וחשבון בבית החולים |
| **משימות** | Data Assets עם יעדים: הגעה למקום, כניסה לרכב, חיסול מטרות, התחמקות מהמשטרה. מרקר בעולם מתחיל משימה, וחלק מהמשימות מוגבלות לאח מסוים |
| **שמירה** | מיקום וכסף של כל אח, משימות שהושלמו, שמירה אוטומטית אחרי החלפה ואחרי משימה |
| **מולטיפלייר** | Host / Find / Join, עד 32 שחקנים כברירת מחדל (ניתן לשינוי). עובד מיד ב-LAN, ומתחברים דרך האינטרנט עם EOS החינמי |
| **ממשק** | בסיס ל-Widget ב-UMG עם אירועים, וכשאין Widget מוצג overlay זמני לבדיקות |
| **שליטה** | Enhanced Input. כל המקשים (מקלדת, עכבר ושלט) נבנים אוטומטית, אז לא צריך ליצור קבצי Input |

---

## דרישות

- **Windows 10/11** (64 ביט)
- **Unreal Engine 5.8** דרך [Epic Games Launcher](https://www.unrealengine.com/download). חינמי עד מיליון דולר הכנסות למשחק, ומעבר לזה 5% תמלוגים
- **Visual Studio 2022** (Community, חינמי) עם ה-workload **"Game development with C++"** והרכיבים שה-Launcher ממליץ עליהם
- **Git** + **Git LFS**
- מחשב: SSD עם **150GB+ פנויים**, **32GB RAM** מומלץ (16 מינימום), כרטיס מסך עם **8GB VRAM ומעלה** (RTX 3060 / RX 6700 ומעלה) בשביל Lumen ו-Nanite

### הרצה על הנייד (ASUS ROG, ‏GTX 1650 עם 4GB)

הנייד מספיק לאב-טיפוס האפור, בתנאי שמורידים את איכות התצוגה בעורך:

1. **חשמל:** עובדים כשהנייד מחובר לחשמל. על סוללה כרטיס המסך מאט מאוד.
2. **כרטיס NVIDIA:** פאנל הבקרה של NVIDIA, ‏**ניהול הגדרות תלת-ממד**, ‏**הגדרות תוכנית**. מוסיפים את `UnrealEditor.exe` ובוחרים **מעבד NVIDIA בעל ביצועים גבוהים**. בלי זה העורך עלול לרוץ על כרטיס Intel UHD.
3. **Armoury Crate:** מצב **Turbo**, ואם יש, מצב GPU של **Ultimate** או **dGPU**.
4. **איכות בעורך:** בחלון התצוגה לוחצים **Settings**, ‏**Engine Scalability Settings**, ובוחרים **Medium**. ב-Medium העורך מכבה את Lumen, והוא חוזר אוטומטית כשמגדירים High על מחשב חזק יותר.
5. **סבלנות בפתיחה הראשונה:** בפעם הראשונה העורך מקמפל shaders, וזה לוקח 30 עד 60 דקות. מהפעם השנייה זה מהיר.

## התקנה, שלב אחר שלב

1. התקן את Epic Games Launcher, ובלשונית **Unreal Engine → Library** התקן את **5.8**.
2. התקן את Visual Studio 2022 עם **Game development with C++**.
3. שכפל את הריפו:
   ```bash
   git lfs install
   git clone https://github.com/aviv112265-jpg/vigilant-invention.git
   ```
4. קליק ימני על **`Mishpacha.uproject`** → **Generate Visual Studio project files**.
5. פתח את `Mishpacha.sln`, בחר **Development Editor / Win64**, ואז **Build** (‏Ctrl+Shift+B).
6. אחרי שהבנייה מצליחה, פתח את `Mishpacha.uproject` (או הרץ עם F5 מ-Visual Studio).

## הקמת עולם הבדיקה הראשון (מפה אפורה)

בעורך:

1. **תוכן התחלתי:** Content Browser → **Add → Add Feature or Content Pack** → הוסף **Third Person** ו-**Vehicle**. זה מביא את הדמויות Manny/Quinn, אנימציות ורכב ספורט.
2. **מפה:** צור Level חדש (Open World או Basic), ושמור אותו בשם `Content/Maps/TelAvir_Greybox`.
   בהגדרות הפרויקט (Maps & Modes) הגדר אותו כ-Editor Startup Map וכ-Game Default Map.
3. **שלושת האחים:** צור 3 Blueprints עם הורה **MishpachaCharacter**: `BP_Rafi`, `BP_Shira`, `BP_Omer`.
   בכל אחד: Mesh = `SKM_Manny`/`SKM_Quinn`, ו-Anim Class = ה-AnimBP של Third Person.
   - ל-`BP_Rafi` הוסף קומפוננטה **RafiBackupAbility** וקבע **Backup Pawn Class**. בהתחלה אפשר פשוט BP_Rafi אחר, ובהמשך NPC אמיתי.
   - ל-`BP_Shira` הוסף **ShiraConnectionsAbility**.
   - ל-`BP_Omer` הוסף **OmerDroneAbility**.
4. **Data Assets:** קליק ימני → Miscellaneous → Data Asset → **SiblingDefinition**. צור שלושה: `DA_Rafi`, `DA_Shira`, `DA_Omer`.
   בכל אחד מלא SiblingId (`Rafi` / `Shira` / `Omer`), שם תצוגה, Pawn Class וצבע.
5. **Game Mode:** צור Blueprint עם הורה **StoryGameMode** בשם `BP_StoryGameMode`. ב-Family Definitions הכנס את שלושת ה-DA לפי הסדר (רפי, שירה, עומר).
   ב-World Settings של המפה בחר את `BP_StoryGameMode`.
6. **נקודות התחלה:** גרור למפה 3 **SiblingSpawnPoint** וקבע לכל אחד SiblingId. גרור גם **PlayerStart** אחד כגיבוי.
7. **רכב:** צור Blueprint עם הורה **MishpachaVehicle** בשם `BP_Car`, והעתק אליו מ-`SportsCar_Pawn` את ה-Skeletal Mesh, ה-Anim BP וה-Wheels. גרור כמה רכבים למפה.
8. **בית חולים:** גרור Actor כלשהו (למשל Target Point) והוסף לו Tag `Hospital`.
9. **ניווט:** הוסף **NavMeshBoundsVolume** שמכסה את המפה, בשביל שוטרים וחיילי משפחה.
10. **Play.** אפשר להחליף בין האחים, לגנוב רכב, ולהעלות חום.

### משטרה (אופציונלי בשלב הזה)

צור Data Asset מסוג **PoliceDispatchConfig**. לכל רמת חום מגדירים מחלקות יחידה (Pawn עם AI Controller) ומספר יחידות מקסימלי.
את ה-Data Asset מחברים ב-`BP_StoryGameMode` דרך **Police Config**.

### משימה ראשונה לדוגמה

1. צור Data Asset מסוג **MissionDefinition** בשם `DA_Mission_Rafi_01`. מלא MissionId `Rafi_01`, ‏Required Sibling `Rafi`, ותגמול.
2. תחת **Objectives** הוסף לפי הסדר: *Enter Vehicle*, ואז *Reach Location* (Target Actor Tag = `Dropoff`), ואז *Lose The Heat*.
3. גרור למפה **MissionStartMarker** ובחר בו את המשימה. סמן Actor כלשהו בתגית `Dropoff`.

### תגיות שהקוד מכיר

| Tag | משמעות |
|---|---|
| `Civilian` | אזרח שיכול להיות עד לפשע |
| `Police` | שוטר: גם עד, ואם הורגים אותו זה CopKilled |
| `Enemy` | אויב, והרחפן של עומר יכול לסמן אותו |
| `Hospital` | נקודת התעוררות אחרי מוות |

> כדי לראות סימון אויבים: צור Post Process Material שמצייר קו מתאר לפי **CustomStencil = 1**, והכנס אותו ל-Post Process Volume.

---

## שליטה

| פעולה | מקלדת ועכבר | שלט |
|---|---|---|
| תנועה | WASD | סטיק שמאלי |
| מבט | עכבר | סטיק ימני |
| ריצה | Shift | לחיצה על סטיק שמאלי |
| קפיצה | רווח | A / ✕ |
| כניסה ויציאה מרכב | F | Y / △ |
| ירי / כיוון | לחצן שמאלי / ימני | RT / LT |
| יכולת מיוחדת (ובזמן רחפן: החזרה) | Q | RB |
| החלפת אח | 1 / 2 / 3 | |
| תפריט החלפה (החזק) | Alt שמאלי | D-Pad למטה |
| **ברכב:** גז / בלם ורוורס / היגוי / בלם יד | W / S / A-D / רווח | RT / LT / סטיק / B |
| **ברכב:** רדיו | R | D-Pad ימינה |
| **ברחפן:** למעלה / למטה | רווח / Shift | |

## מולטיפלייר

**כמה שחקנים?** ברירת המחדל היא **32** (‏`Config/DefaultGame.ini` → `MaxPlayers`). מה שמגביל באמת הוא קצב ההעלאה של המארח או חוזק השרת.
מארח רגיל בבית מחזיק בנוחות בערך 8–16 שחקנים, ושרת ייעודי מגיע ל-32 ומעלה.

### בדיקה במחשב אחד

ליד כפתור Play: **Net Mode = Play As Listen Server**, ‏**Number of Players = 2–4**.

### ברשת הביתית (LAN)

עובד מיד עם OnlineSubsystem Null. מתוך Blueprint (למשל תפריט ראשי) קוראים ל-
`GetGameInstance → GetSessions → HostSession(32, LAN=true, "/Game/Maps/TelAvir_Greybox")`,
ואצל החברים: `FindSessions` ואז `JoinSessionByIndex`.
אפשרות פשוטה יותר: המארח פותח את המפה עם `?listen`, והחברים מקלידים בקונסול `open <IP-של-המארח>`.

### דרך האינטרנט: Epic Online Services (חינמי)

1. היכנס ל-[Epic Developer Portal](https://dev.epicgames.com/portal) וצור Product. אין עלות.
2. צור Client ו-Application, וקבל Product ID, ‏Sandbox ID, ‏Deployment ID, ‏Client ID ו-Secret.
3. בעורך: **Project Settings → Plugins → Epic Online Services**, מלא את הפרטים.
4. ב-`Config/DefaultEngine.ini` הפעל את בלוק ה-EOS (יש שם הוראות) במקום `DefaultPlatformService=Null`.
5. קרא ל-`HostSession` עם `bLAN=false`. החיבור עובר דרך P2P של EOS, כך שלא צריך לפתוח פורטים בנתב.

### שרת ייעודי חינמי (מתקדם)

- דורש **מנוע שנבנה מקוד מקור** (‏GitHub של Epic). ה-Launcher לא בונה את `MishpachaServer.Target.cs`.
- אפשרות חינמית: **Oracle Cloud Always Free**, שרת Ampere ARM עם 4 ליבות ו-24GB RAM. בונים את השרת ל-**LinuxArm64**, פותחים פורט UDP ‏7777, וכך מקבלים שרת שרץ 24/7 גם כשאף אחד מכם לא מארח.

---

## מבנה תיקיות

```
Mishpacha.uproject              הגדרות הפרויקט והפלאגינים
Config/                         הגדרות מנוע, משחק ושליטה
Source/Mishpacha/
  Public|Private/
    Core/        GameModes (Story/Online), PlayerController, PlayerState, GameState, GameInstance
    Characters/  MishpachaCharacter (בסיס לכל הדמויות), MishpachaWeapon
    Vehicles/    MishpachaVehicle (Chaos)
    Family/      FamilySubsystem (החלפת אחים), SiblingDefinition, SiblingSpawnPoint
    Abilities/   יכולות: רפי / שירה / עומר + DronePawn
    Heat/        HeatComponent (חום), CrimeLibrary, PoliceDispatch
    Missions/    MissionDefinition, Objectives, MissionSubsystem, MissionStartMarker
    Save/        MishpachaSaveGame
    Online/      MishpachaSessionSubsystem (Host/Find/Join)
    UI/          MishpachaHUD + MishpachaHUDWidget (בסיס ל-UMG)
```

## עברית ואנגלית

- כל הטקסטים בקוד הם `FText` עם `LOCTEXT` באנגלית. התרגום לעברית נעשה ב-**Tools → Localization Dashboard** (‏Gather, ואז Translate ל-`he`).
- ב-UMG מגדירים לטקסטים **Flow Direction = Culture** כדי שעברית תוצג מימין לשמאל.
- גופן: כדאי לייבא גופן שתומך בעברית, כמו Heebo, ‏Assistant או Rubik מ-Google Fonts (רישיון OFL חינמי).

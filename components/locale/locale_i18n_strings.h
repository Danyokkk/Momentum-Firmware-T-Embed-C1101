/**
 * @file locale_i18n_strings.h
 * Translation table for the T-Embed CC1101 Plus UI (English, Spanish, Russian).
 *
 * This file is included multiple times with LOC_X defined differently (X-macro)
 * to generate both the LocStrId enum and the string table, keeping the three
 * languages aligned row-by-row. English is the fallback: a NULL in the ES or RU
 * column falls back to the English text.
 *
 * Encoding: UTF-8. Spanish rows use accented Latin-1 letters; Russian rows use
 * Cyrillic. These render only under the matching per-language font selected by
 * canvas_set_ui_language(); see locale_i18n.c.
 *
 *     LOC_X(Identifier,            English,             Spanish,              Russian)
 */

/* First-boot language picker */
LOC_X(LocStrChooseLanguage,        "Choose language",   "Elige idioma",       "Выберите язык")
LOC_X(LocStrLanguage,              "Language",          "Idioma",             "Язык")

/* Common actions / dialog buttons */
LOC_X(LocStrOk,                    "OK",                "OK",                 "ОК")
LOC_X(LocStrCancel,                "Cancel",            "Cancelar",           "Отмена")
LOC_X(LocStrBack,                  "Back",              "Atrás",              "Назад")
LOC_X(LocStrExit,                  "Exit",              "Salir",              "Выход")
LOC_X(LocStrYes,                   "Yes",               "Sí",                 "Да")
LOC_X(LocStrNo,                    "No",                "No",                 "Нет")
LOC_X(LocStrSave,                  "Save",              "Guardar",            "Сохранить")
LOC_X(LocStrDelete,                "Delete",            "Eliminar",           "Удалить")
LOC_X(LocStrContinue,              "Continue",          "Continuar",          "Продолжить")
LOC_X(LocStrOn,                    "ON",                "SÍ",                 "ВКЛ")
LOC_X(LocStrOff,                   "OFF",               "NO",                 "ВЫКЛ")

/* Status / progress */
LOC_X(LocStrLoading,               "Loading...",        "Cargando...",        "Загрузка...")
LOC_X(LocStrPleaseWait,            "Please wait",       "Espera por favor",   "Подождите")
LOC_X(LocStrDone,                  "Done",              "Hecho",              "Готово")
LOC_X(LocStrError,                 "Error",             "Error",              "Ошибка")

/* SD card */
LOC_X(LocStrNoSdCard,              "No SD card",        "Sin tarjeta SD",     "Нет SD-карты")
LOC_X(LocStrSdCardError,           "SD card error",     "Error de tarjeta SD","Ошибка SD-карты")
LOC_X(LocStrSdCardFull,            "SD card full",      "Tarjeta SD llena",   "SD-карта заполнена")
LOC_X(LocStrInsertSdCard,          "Insert an SD card", "Inserta una SD",     "Вставьте SD-карту")

/* Settings menu labels */
LOC_X(LocStrSettings,              "Settings",          "Ajustes",            "Настройки")
LOC_X(LocStrSystem,                "System",            "Sistema",            "Система")
LOC_X(LocStrHandOrient,            "Hand Orient",       "Orientación",        "Ориентация")
LOC_X(LocStrUnits,                 "Units",             "Unidades",           "Единицы")
LOC_X(LocStrTimeFormat,            "Time Format",       "Formato hora",       "Формат времени")
LOC_X(LocStrDateFormat,            "Date Format",       "Formato fecha",      "Формат даты")
LOC_X(LocStrLogLevel,              "Log Level",         "Nivel de registro",  "Уровень лога")
LOC_X(LocStrLogDevice,             "Log Device",        "Disp. de registro",  "Устройство лога")
LOC_X(LocStrLogBaudRate,           "Log Baud Rate",     "Baudios registro",   "Скорость лога")
LOC_X(LocStrDebug,                 "Debug",             "Depuración",         "Отладка")
LOC_X(LocStrHeapTrace,             "Heap Trace",        "Rastreo heap",       "Трассировка кучи")
LOC_X(LocStrSleepMethod,           "Sleep Method",      "Modo de reposo",     "Режим сна")
LOC_X(LocStrFileNaming,            "File Naming",       "Nombres archivo",    "Имена файлов")

/* Main menu categories */
LOC_X(LocStrApplications,          "Applications",      "Aplicaciones",       "Приложения")
LOC_X(LocStrTools,                 "Tools",             "Herramientas",       "Инструменты")

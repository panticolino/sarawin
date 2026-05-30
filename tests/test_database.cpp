#include "data/database.h"
#include "util/logger.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>

void initResources() { Q_INIT_RESOURCE(saralibre); }

/**
 * Test de la capa de base de datos.
 * Verifica: apertura, schema, CRUD básico, transacciones.
 */

void testOpenAndSchema(sara::Database& db)
{
    assert(db.open() && "Database should open successfully");
    assert(db.isOpen() && "Database should report as open");
    assert(db.schemaVersion() == 1 && "Schema version should be 1 after initial setup");

    std::cout << "  ✓ open & schema\n";
}

void testCrudSchedules(sara::Database& db)
{
    // Insertar una programación
    auto q = db.execPrepared(
        "INSERT INTO schedules (id, name) VALUES (?, ?)",
        {"test_sched_1", "Mañanas de Salsa"}
    );
    assert(q.has_value() && "Insert schedule should succeed");

    // Leer
    auto q2 = db.execPrepared(
        "SELECT name FROM schedules WHERE id = ?",
        {"test_sched_1"}
    );
    assert(q2.has_value() && "Select should succeed");
    assert(q2->next() && "Should have one row");
    assert(q2->value(0).toString() == "Mañanas de Salsa");

    // Insertar elemento
    db.execPrepared(
        "INSERT INTO schedule_elements (schedule_id, position, type, path, display_name) "
        "VALUES (?, ?, ?, ?, ?)",
        {"test_sched_1", 0, "folder", "/home/radio/salsa", "Carpeta Salsa"}
    );
    db.execPrepared(
        "INSERT INTO schedule_elements (schedule_id, position, type, path, display_name) "
        "VALUES (?, ?, ?, ?, ?)",
        {"test_sched_1", 1, "time_announce", "", "Locución Hora"}
    );

    auto q3 = db.execPrepared(
        "SELECT COUNT(*) FROM schedule_elements WHERE schedule_id = ?",
        {"test_sched_1"}
    );
    assert(q3.has_value() && q3->next());
    assert(q3->value(0).toInt() == 2 && "Should have 2 elements");

    // Eliminar cascada
    db.execPrepared("DELETE FROM schedules WHERE id = ?", {"test_sched_1"});
    auto q4 = db.execPrepared(
        "SELECT COUNT(*) FROM schedule_elements WHERE schedule_id = ?",
        {"test_sched_1"}
    );
    assert(q4.has_value() && q4->next());
    assert(q4->value(0).toInt() == 0 && "Cascade delete should remove elements");

    std::cout << "  ✓ CRUD schedules + cascade delete\n";
}

void testTransaction(sara::Database& db)
{
    // Transacción exitosa
    bool ok = db.transaction([&]() {
        db.execPrepared(
            "INSERT INTO schedules (id, name) VALUES (?, ?)",
            {"txn_1", "Test Transacción"}
        );
        return true;
    });
    assert(ok && "Transaction should succeed");

    auto q = db.execPrepared("SELECT name FROM schedules WHERE id = ?", {"txn_1"});
    assert(q.has_value() && q->next() && "Row should exist after commit");

    // Transacción con rollback
    bool ok2 = db.transaction([&]() {
        db.execPrepared(
            "INSERT INTO schedules (id, name) VALUES (?, ?)",
            {"txn_2", "Debe hacer rollback"}
        );
        return false;  // Forzar rollback
    });
    assert(!ok2 && "Transaction should report failure");

    auto q2 = db.execPrepared("SELECT name FROM schedules WHERE id = ?", {"txn_2"});
    assert(q2.has_value() && !q2->next() && "Row should NOT exist after rollback");

    // Limpiar
    db.execPrepared("DELETE FROM schedules WHERE id = ?", {"txn_1"});

    std::cout << "  ✓ transactions (commit + rollback)\n";
}

void testPlayHistory(sara::Database& db)
{
    db.execPrepared(
        "INSERT INTO play_history (file_path, source, duration_ms) VALUES (?, ?, ?)",
        {"/music/track1.mp3", "fallback", 180000}
    );
    db.execPrepared(
        "INSERT INTO play_history (file_path, source, duration_ms) VALUES (?, ?, ?)",
        {"/music/track2.mp3", "schedule:Mañanas", 240000}
    );

    auto q = db.execPrepared("SELECT COUNT(*) FROM play_history");
    assert(q.has_value() && q->next());
    assert(q->value(0).toInt() >= 2);

    // Limpiar
    db.exec("DELETE FROM play_history");

    std::cout << "  ✓ play_history\n";
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    initResources();

    // Usar directorio temporal para no afectar datos reales
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());

    // Override de la ruta por variable de entorno no aplica aquí,
    // pero Database usa QStandardPaths. Para test, creamos una DB directa.
    // Hacemos un workaround: configurar XDG_CONFIG_HOME
    qputenv("XDG_CONFIG_HOME", tmpDir.path().toUtf8());

    sara::initLogger("warn");

    std::cout << "\n── Test Database ──────────────────────\n";

    sara::Database db;
    testOpenAndSchema(db);
    testCrudSchedules(db);
    testTransaction(db);
    testPlayHistory(db);

    db.close();

    std::cout << "\n  Todos los tests pasaron ✓\n\n";
    return 0;
}

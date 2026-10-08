#include "app/AppContext.h"

#include <filesystem>

#include "repositories/JsonDatabase.h"

namespace wallet
{

    std::ostream *AppContext::openLog(std::ofstream &f, const AppOptions &o, const std::string &dir)
    {
        if (o.inMemory || !o.fileLogging)
            return nullptr;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        f.open(dir + "/app.log", std::ios::app);
        return f.is_open() ? &f : nullptr;
    }

    AppContext::AppContext(AppConfig cfg, const IClock &clk, std::unique_ptr<Database> database, const AppOptions &options)
        : config(std::move(cfg)),
          clock(clk),
          db(std::move(database)),
          log(openLog(logFile, options, options.dataDir), parseLogLevel(config.logLevel), clk),
          hasher(config.passwordIterations),
          sessions(ids),
          access(sessions),
          audit(db->audit(), ids, clk),
          inbox(std::make_shared<InAppNotification>()),
          notifier(db->users(), events, clk),
          settlement(db->wallets(), db->ledger(), ids, clk),
          fraud(FraudEngine::standard(config.fraud)),
          auth(*db, sessions, hasher, audit, ids, clk, config, log),
          payments(PaymentService::Dependencies{*db, access, settlement, fraud, reviewQueue, notifier, audit, ids, clk, config, log}),
          refunds(RefundService::Dependencies{*db, access, settlement, notifier, audit, ids, clk, log}),
          admin(*db, access, audit, notifier, reviewQueue, config),
          query(*db, access),
          reports(*db, access)
    {
        // Notification channels (Observer subscribers). Email/SMS/Push are simulated: they write to an outbox log.
        std::ostream *outbox = &discardedOutbox;
        if (!options.inMemory)
        {
            outboxFile.open(options.dataDir + "/outbox.log", std::ios::app);
            if (outboxFile.is_open())
                outbox = &outboxFile;
        }
        events.subscribe(inbox);
        events.subscribe(std::make_shared<EmailNotification>(*outbox));
        events.subscribe(std::make_shared<SmsNotification>(*outbox));
        events.subscribe(std::make_shared<PushNotification>(*outbox));
        if (options.consoleNotifications)
            events.subscribe(std::make_shared<ConsoleNotification>(std::cout));
    }

    Result<std::unique_ptr<AppContext>> AppContext::create(const AppOptions &options, const IClock &clock)
    {
        AppConfig cfg = AppConfig::defaults();
        cfg.dataDir = options.dataDir;
        if (!options.inMemory && std::filesystem::exists(options.configPath))
        {
            auto loaded = AppConfig::loadFromFile(options.configPath);
            if (!loaded)
                return fail(loaded.error());
            cfg = loaded.value();
            cfg.dataDir = options.dataDir;
        }

        if (options.passwordIterations > 0)
            cfg.passwordIterations = options.passwordIterations;

        std::unique_ptr<Database> db;
        if (options.inMemory)
        {
            db = std::make_unique<InMemoryDatabase>();
        }
        else
        {
            auto json = std::make_unique<JsonDatabase>(options.dataDir);
            if (auto r = json->load(); !r)
                return fail(r.error());
            db = std::move(json);
        }

        std::unique_ptr<AppContext> ctx(new AppContext(std::move(cfg), clock, std::move(db), options));

        // Re-seed id counters so new ids never collide with persisted ones.
        for (const auto &u : ctx->db->users().all())
        {
            ctx->ids.observe(u->id());
            if (auto m = std::dynamic_pointer_cast<Merchant>(u))
                ctx->ids.observe(m->paymentId());
        }
        for (const auto &w : ctx->db->wallets().all())
            ctx->ids.observe(w.id());
        auto txs = ctx->db->transactions().all();
        for (const auto &t : txs)
            ctx->ids.observe(t.id());
        for (const auto &e : ctx->db->ledger().all())
            ctx->ids.observe(e.id);
        for (const auto &r : ctx->db->refunds().all())
            ctx->ids.observe(r.id);
        for (const auto &a : ctx->db->audit().all())
            ctx->ids.observe(a.id);
        ctx->reviewQueue.rebuildFrom(txs);

        bool hasAdmin = false;
        for (const auto &u : ctx->db->users().all())
            hasAdmin = hasAdmin || u->role() == Role::Admin;
        if (!hasAdmin)
        {
            auto r = ctx->auth.createAdmin("System Administrator", kDefaultAdminEmail, kDefaultAdminPassword);
            if (!r)
                return fail(r.error());
        }
        return ctx;
    }

} // namespace wallet

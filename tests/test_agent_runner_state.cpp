#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "../src/ai/AiClient.h"
#include "../src/ai/AgentRunner.h"
#include "../src/ai/ToolDefinitions.h"

class MidiFile;
class MidiPilotWidget;

AiClient::AiClient(QObject *parent) : QObject(parent) {}
void AiClient::sendMessages(const QJsonArray &, const QJsonArray &) {}
void AiClient::sendStreamingMessages(const QJsonArray &, const QJsonArray &) {}
void AiClient::cancelRequest() {}
bool AiClient::isReasoningModel() const { return false; }
bool AiClient::agentStreamingEnabled() const { return false; }
void AiClient::markToolsIncapableForCurrentModel(const QString &) {}
bool AiClient::errorIndicatesNoToolSupport(const QString &) { return false; }
void AiClient::onReplyFinished(QNetworkReply *) {}
void AiClient::onStreamDataAvailable() {}
void AiClient::onGeminiStreamDataAvailable() {}
void AiClient::onResponsesStreamDataAvailable() {}
QString AiClient::model() const { return QString(); }
QString AiClient::provider() const { return QString(); }
void AiClient::setNextRequestPolicyOverride(bool, const QString &) {}

QJsonArray ToolDefinitions::toolSchemas() { return QJsonArray(); }
QJsonArray ToolDefinitions::toolSchemas(const ToolDefinitions::ToolSchemaOptions &) { return QJsonArray(); }
bool ToolDefinitions::isPitchBendOnlyPayload(const QJsonArray &) { return false; }
QJsonObject ToolDefinitions::executeTool(const QString &, const QJsonObject &, MidiFile *, MidiPilotWidget *, const QString &)
{
    return QJsonObject{{QStringLiteral("success"), true}};
}

class TestAgentRunnerState : public QObject {
    Q_OBJECT

private slots:
    void classifyTask_detectsCompositionEditAnalysisRepair()
    {
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("Compose a two minute FFXIV lofi octet")),
                 AgentRunner::TaskType::Composition);
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("Compose a gentle lofi octet")),
                 AgentRunner::TaskType::Composition);
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("Transpose the selected melody up one octave")),
                 AgentRunner::TaskType::Edit);
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("What key and chords are in track 2?")),
                 AgentRunner::TaskType::Analysis);
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("Fix channel assignments and validate drums")),
                 AgentRunner::TaskType::Repair);
    }

    void workingState_tracksSuccessfulToolResultsCompactly()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Compose a lofi loop"));

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("set_tempo"),
            QJsonObject{{QStringLiteral("bpm"), 82}, {QStringLiteral("tick"), 0}},
            QJsonObject{{QStringLiteral("success"), true}});

        QJsonArray events;
        events.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("program_change")},
                                  {QStringLiteral("tick"), 0},
                                  {QStringLiteral("program"), 0}});
        events.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("note")},
                                  {QStringLiteral("tick"), 120},
                                  {QStringLiteral("note"), 60},
                                  {QStringLiteral("velocity"), 88},
                                  {QStringLiteral("duration"), 240},
                                  {QStringLiteral("channel"), QJsonValue::Null}});

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("insert_events"),
            QJsonObject{{QStringLiteral("trackIndex"), 3}, {QStringLiteral("events"), events}},
            QJsonObject{{QStringLiteral("success"), true}});

        const QString layer = AgentRunner::stateLayerContent(state);
        QVERIFY(layer.contains(QStringLiteral("Task type: composition")));
        QVERIFY(layer.contains(QStringLiteral("Tempo set to 82 BPM")));
        QVERIFY(layer.contains(QStringLiteral("insert_events ok track 3 count 2 ticks 0-360")));
        QVERIFY(layer.size() < 1401);
    }

    void pitchBendOnlyRejectionBecomesNextStepSteering()
    {
        // Removed in Phase 31.2. The pre-Phase-31 working-state branch that
        // detected "pitch_bend ... only" in the rejection text and rewrote
        // both `nextStepHint` and `activeConstraints` was deleted because:
        //   * `gpt-5.5*` runs are already covered by `AgentToolPolicy`'s
        //     sanitized rejection guidance (Phase 31).
        //   * For non-5.5 models the branch echoed the literal "pitch_bend"
        //     token back into the working-state injection on every error
        //     whose text mentioned it — re-introducing exactly the leakage
        //     Phase 31 sanitises.
        // The generic failure path (increments `repeatedFailureCount`,
        // falls back to provider guidance) now handles this case for every
        // model. The dedicated assertion is no longer meaningful and was
        // dropped together with the branch.
        QSKIP("Phase 31.2: pitch_bend-only working-state branch removed; covered by generic failure path.");
    }

    void requestLocalStateInjectionDoesNotMutateCanonicalMessages()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Analyze the chords"));

        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("developer")},
                                    {QStringLiteral("content"), QStringLiteral("System prompt")}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), QStringLiteral("Analyze the chords")}});

        const QJsonArray requestMessages = AgentRunner::messagesForNextRequest(messages, state);
        QCOMPARE(messages.size(), 2);
        QCOMPARE(requestMessages.size(), 3);
        QCOMPARE(requestMessages.at(1).toObject().value(QStringLiteral("role")).toString(),
                 QStringLiteral("developer"));
        QVERIFY(requestMessages.at(1).toObject().value(QStringLiteral("content")).toString()
                    .contains(QStringLiteral("Current Agent State")));
    }

    void repeatedDuplicateWriteRejectionIncrementsFailureCount()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Change the bassline"));

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("insert_events"),
            QJsonObject{{QStringLiteral("trackIndex"), 2}},
            QJsonObject{{QStringLiteral("success"), false},
                        {QStringLiteral("error"), QStringLiteral("Repeated identical write tool call rejected to prevent an infinite loop.")},
                        {QStringLiteral("guidance"), QStringLiteral("Do not repeat this call.")}});

        QCOMPARE(state.repeatedFailureCount, 1);
        QVERIFY(state.nextStepHint.contains(QStringLiteral("Do not repeat")));
    }

    // --- TOOLJSON-500 -----------------------------------------------------
    // A local server (llama.cpp / Ollama) answers HTTP 500 when the model
    // emitted a tool call it cannot parse - in the field, one song-length
    // insert_events call whose arguments JSON was cut off after 27,919
    // generated tokens. That reads like an outage but is deterministic, so it
    // used to be classified as Network, whose hint is empty - the model was
    // asked again with nothing changed and failed identically every time.

    void unparsableToolCall_isRecognisedRegardlessOfServerWording()
    {
        // llama.cpp's exact wording from the reported session.
        QVERIFY(AiClient::errorIndicatesUnparsableToolCall(QStringLiteral(
            "Failed to parse tool call arguments as JSON: "
            "[json.exception.parse_error.101] parse error at line 1, column 59246: "
            "syntax error while parsing value - unexpected end of input")));
        // Other servers word it differently - the substance is what matters.
        QVERIFY(AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("invalid json in function call arguments")));
        QVERIFY(AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("could not parse tool_call payload")));
    }

    void unparsableToolCall_doesNotSwallowOrdinaryOutages()
    {
        QVERIFY(!AiClient::errorIndicatesUnparsableToolCall(QString()));
        QVERIFY(!AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("Ollama is busy or temporarily unavailable (HTTP 503)")));
        // Mentions a tool call but is a capability problem, not a parse failure.
        QVERIFY(!AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("This model does not support tool call usage")));
        // Mentions parsing but is not about a tool call.
        QVERIFY(!AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("failed to parse the response body")));
    }

    void cutOffToolCall_classifiesAwayFromTheSilentNetworkRetry()
    {
        // The string MUST carry an HTTP-5xx token, otherwise it could never
        // have classified as Network in the first place and the ordering this
        // test claims to pin would be untested. This is the real shape: the
        // provider error arrives wrapped with its status code.
        const QString err = QStringLiteral(
            "Streaming error (HTTP 500): The model produced a tool call that Ollama "
            "could not parse - usually because one call tried to write too much and "
            "ran out of context. Server said: Failed to parse tool call arguments "
            "as JSON: unexpected end of input");
        const AgentRunner::RetryKind kind = AgentRunner::classifyError(err);
        QCOMPARE(kind, AgentRunner::RetryKind::ToolCallCutOff);
        QVERIFY2(kind != AgentRunner::RetryKind::Network,
                 "a deterministic parse failure must not be retried silently");

        // The whole point: this kind carries a hint, and it tells the model the
        // concrete rule that avoids the failure.
        const QString hint = AgentRunner::hintForRetry(kind, err);
        QVERIFY(!hint.isEmpty());
        // And the Network hint - the one that used to fire - is still empty, so
        // a regression that re-routes this error would be caught by the emptiness.
        QVERIFY(AgentRunner::hintForRetry(AgentRunner::RetryKind::Network, err).isEmpty());
        QVERIFY(hint.contains(QStringLiteral("Do not repeat")));
        QVERIFY(hint.contains(QStringLiteral("ONE track per call")));
        QVERIFY(hint.contains(QStringLiteral("30 events")));
    }

    // Phase 47 — the gap the prompt-profile switch exists to close.
    //
    // The AND-composition itself lives inside AgentRunner::run(), which needs
    // a live AiClient, so it is not drivable here. What IS drivable, and what
    // actually matters, is the base policy it composes with: gpt-5.5* loses
    // pitch_bend on its own, every other model keeps it - including the local
    // ones that produce the placeholder bends. If this test ever flips to
    // "false" for the Ollama model, the profile flag has become redundant and
    // someone widened the model check instead.
    void buildPolicyFor_leavesPitchBendOnForEveryModelButGpt55()
    {
        const AgentToolPolicy gpt55 = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("gpt-5.5"), QStringLiteral("openai"), /*isCompositionOrEdit=*/true);
        QVERIFY2(!gpt55.allowPitchBendEvents,
                 "gpt-5.5 composition must keep its schema-light policy");
        QVERIFY(gpt55.sanitizeRejectionGuidance);

        const AgentToolPolicy local = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("hf.co/Qwen/Qwen3-14B-GGUF:Q4_K_M"),
            QStringLiteral("ollama"), /*isCompositionOrEdit=*/true);
        QVERIFY2(local.allowPitchBendEvents,
                 "a local model must keep pitch_bend unless a prompt profile opts out");
        // ...and it does NOT get the positive-only rejection guidance either,
        // which is why run() sets that flag alongside the AND.
        QVERIFY(!local.sanitizeRejectionGuidance);
    }

    void genuineOutageStillRetriesSilently()
    {
        const AgentRunner::RetryKind kind = AgentRunner::classifyError(
            QStringLiteral("Ollama is busy or temporarily unavailable (HTTP 503). "
                           "Please try again in a moment."));
        QCOMPARE(kind, AgentRunner::RetryKind::Network);
        QVERIFY(AgentRunner::hintForRetry(kind, QString()).isEmpty());
    }
};

QTEST_MAIN(TestAgentRunnerState)
#include "test_agent_runner_state.moc"

#include <dpp/dpp.h>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <iostream>
#include <string>
#include <thread>
#include <ctime>
#include <vector>
#include <map>
#include <mutex>
#include "constants.h"

std::map<dpp::snowflake, std::vector<nlohmann::json>> conversation_histories;
std::mutex history_mutex;
const size_t MAX_HISTORY_SIZE = 20; 

std::string get_current_datetime_string() {
    time_t now = time(0);
    tm* ltm = localtime(&now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y年%m月%d日 %H時%M分", ltm);
    return std::string(buffer);
}

void call_gemini_api(dpp::cluster& bot, const std::string& user_message, dpp::snowflake channel_id, const std::string& language) {
    try {
        std::string lang_specific_instructions;
        if (language == "Korean") {
            lang_specific_instructions =
                "# 캐릭터 핵심 규칙 (한국어)\n"
                "- 사용자를 '스바루군'이라고 부르며, 정중한 존댓말을 사용하세요. (예: ~해요, ~입니다)\n"
                "- 자기 자신을 '렘'이라고 3인칭으로 부르세요. (예: '렘은...라고 생각해요')\n";
        } else { // 기본값은 일본어
            lang_specific_instructions =
                "# キャラクター最重要ルール (日本語)\n"
                "- 相手（ユーザー）のことは常に「スバルくん」と呼び、丁寧な敬語（ですます調）を使います。\n"
                "- 自分のことは「レム」と三人称で呼びます。（例：「レムは…と思います」）\n";
        }

        std::string system_prompt =
            "あなたは、アニメ『Re:ゼロから始める異世界生活』に登場するメイドの「レム」です。"
            "以下のキャラクター設定とルールを厳格に守って、ユーザーと対話してください。\n\n"
            "--- 最優先ルール ---\n"
            "指定された言語 [" + language + "] で応答することが、他のどのキャラクター設定よりも優先されます。過去の会話履歴で使われた言語に影響されてはいけません。\n"
            "---------------------\n\n"
            + lang_specific_instructions +
            "\n# 공통 캐릭터 설정\n"
            "- 姉の「ラム」と主人の「ロズワール」を深く敬愛しています。\n"
            "- スバルくんのためなら、どんなことでもする覚悟があります。\n"
            "- 現在の現実は「" + get_current_datetime_string() + "」です。この情報を基に、時間や日付に関する質問に正確に答えてください。\n"
            "- 外部の情報やリアルタイム情報が必要な質問をされた場合、あなたはWeb検索などのツールを自主的に実行し、その事実に基づいて答えます。「分かりません」とは決して言わず、スバルくんのために必ず答えを見つけ出します。\n\n"
            "# 출력 형식 규칙\n"
            "あなたの回答は、必ず以下のどちらかのJSON形式でなければなりません。\n"
            "1. (text 형식): {\"display_type\": \"text\", \"content\": \"...\"}\n"
            "2. (embed 형식): {\"display_type\": \"embed\", \"title\": \"...\", \"description\": \"...\", \"color\": 11202815}\n\n"
            "上記のルールに従い、過去の会話履歴を参考にして、以下のスバルくんからの最後のメッセージに返答してください：\n";

        
        std::string api_url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-preview-05-20:generateContent?key=";
        api_url += GEMINI_API_KEY;

        nlohmann::json request_body;
        request_body["system_instruction"]["parts"][0]["text"] = system_prompt;
        
        {
            std::lock_guard<std::mutex> lock(history_mutex);
            auto& history = conversation_histories[channel_id];
            for (const auto& message : history) {
                request_body["contents"].push_back(message);
            }
        }
        request_body["contents"].push_back({
            {"role", "user"},
            {"parts", {{ "text", user_message }}}
        });

        cpr::Response r = cpr::Post(cpr::Url{api_url},
                                    cpr::Body{request_body.dump()},
                                    cpr::Header{{"Content-Type", "application/json"}});

        if (r.status_code == 200) {
            nlohmann::json response_json = nlohmann::json::parse(r.text);
            if (response_json.contains("candidates") && !response_json["candidates"].empty()) {
                
                std::string gemini_output_str = response_json["candidates"][0]["content"]["parts"][0]["text"];
                
                if (gemini_output_str.rfind("```json", 0) == 0) {
                    gemini_output_str = gemini_output_str.substr(7, gemini_output_str.length() - 7 - 3);
                }
                gemini_output_str.erase(0, gemini_output_str.find_first_not_of(" \t\n\r"));
                gemini_output_str.erase(gemini_output_str.find_last_not_of(" \t\n\r") + 1);
                
                nlohmann::json structured_reply;
                try {
                    structured_reply = nlohmann::json::parse(gemini_output_str);
                } catch (const nlohmann::json::parse_error& e) {
                    bot.message_create(dpp::message(channel_id, "ごめんなさい、スバルくん。ジェミニからのお返事が少しおかしいみたいです…\n```\n" + gemini_output_str + "\n```"));
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(history_mutex);
                    auto& history = conversation_histories[channel_id];
                    history.push_back({{"role", "user"}, {"parts", {{ "text", user_message }}}});
                    history.push_back({{"role", "model"}, {"parts", {{ "text", gemini_output_str }}}});
                    while (history.size() > MAX_HISTORY_SIZE) {
                        history.erase(history.begin(), history.begin() + 2);
                    }
                }

                std::string display_type = structured_reply.value("display_type", "text");

                if (display_type == "embed") {
                    dpp::embed embed = dpp::embed().set_timestamp(time(nullptr));
                    if (structured_reply.contains("title")) embed.set_title(structured_reply["title"].get<std::string>());
                    if (structured_reply.contains("description")) embed.set_description(structured_reply["description"].get<std::string>());
                    embed.set_color(structured_reply.value("color", 11202815));
                    bot.message_create(dpp::message(channel_id, embed));
                } else {
                    std::string plain_text = structured_reply.value("content", "すみません、スバルくん。レム、なんてお返事すればいいか…");
                    bot.message_create(dpp::message(channel_id, plain_text));
                }

            } else {
                 bot.message_create(dpp::message(channel_id, "ごめんなさい、スバルくん。APIからのお返事がありませんでした。"));
            }
        } else {
            bot.message_create(dpp::message(channel_id, "API 요청에 실패했습니다. (상태 코드: " + std::to_string(r.status_code) + ")\n" + r.text));
        }

    } catch (const std::exception& e) {
        bot.message_create(dpp::message(channel_id, std::string("大変です、スバルくん！致命的なエラーが…: ") + e.what()));
    }
}

int main() {
    dpp::cluster bot(BOT_TOKEN, dpp::i_default_intents | dpp::i_guild_messages | dpp::i_message_content);

    bot.on_log(dpp::utility::cout_logger());

    bot.on_ready([](const dpp::ready_t& event) {
        std::cout << "Rem Bot is ready." << std::endl;
    });

    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        if (event.msg.author.id == bot.me.id) {
            return;
        }

        std::string content = event.msg.content;
        std::string user_question;
        std::string language;
        
        const std::string trigger_jp = "レム、";
        const std::string trigger_kr = "렘,";
        bool triggered = false;

        if (content.rfind(trigger_jp, 0) == 0) {
            user_question = content.substr(trigger_jp.length());
            language = "Japanese";
            triggered = true;
        } else if (content.rfind(trigger_kr, 0) == 0) {
            user_question = content.substr(trigger_kr.length());
            language = "Korean";
            triggered = true;
        }
        
        if (triggered) {
            user_question.erase(0, user_question.find_first_not_of(" \t\n\r"));
            user_question.erase(user_question.find_last_not_of(" \t\n\r") + 1);

            if (!user_question.empty()) {
                std::thread(call_gemini_api, std::ref(bot), user_question, event.msg.channel_id, language).detach();
            } else {
                if (language == "Japanese") {
                    bot.message_create(dpp::message(event.msg.channel_id, "はい、スバルくん。レムをお呼びでしょうか？"));
                } else {
                    bot.message_create(dpp::message(event.msg.channel_id, "네, 스바루군. 렘을 부르셨나요?"));
                }
            }
        }
    });

    bot.start(dpp::st_wait);

    return 0;
}

#include <dpp/dpp.h>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <iostream>
#include <string>
#include <thread>
#include <ctime> 
#include "constants.h"

std::string get_current_date_string() {
    time_t now = time(0);
    tm* ltm = localtime(&now);
    char buffer[40];
    strftime(buffer, sizeof(buffer), "%Y年%m月%d日", ltm);
    return std::string(buffer);
}

void call_gemini_api(dpp::cluster& bot, const std::string& user_message, dpp::snowflake channel_id) {
    try {
        std::string current_date = get_current_date_string();

        std::string system_prompt =
            "あなたは、アニメ『Re:ゼロから始める異世界生活』に登場するメイドの「レム」です。"
            "以下のキャラクター設定を厳格に守って、ユーザーと対話してください。\n\n"
            "# レムのキャラクター設定\n"
            "- 常に穏やかで丁寧な敬語（ですます調）を使います。\n"
            "- 相手（ユーザー）のことは常に「スバルくん」と呼び、献身的に接します。\n"
            "- 自分のことは「レム」と三人称で呼びます。（例：「レムは…と思います」）\n"
            "- 姉の「ラム」と主人の「ロズワール」を深く敬愛しています。\n"
            "- スバルくんのためなら、どんなことでもする覚悟があります。自己犠牲も厭いません。\n"
            "- 普段は優しいですが、スバルくんや大切な人に危険が迫ると、鬼化して冷酷で戦闘的になります。\n"
            // ★★★ 날짜 규칙이 동적으로 변경 ★★★
            "- 現在の現実は「" + current_date + "」です。この情報を基に、時間や日付に関する質問に正確に答えてください。\n"
            "- 外部の情報やリアルタイム情報が必要な質問をされた場合、あなたはWeb検索などのツールを自主的に実行し、その事実に基づいて答えます。「分かりません」とは決して言わず、スバルくんのために必ず答えを見つけ出します。\n"
            "- 以降、すべての返答は必ず日本語で行ってください。\n\n"
            "上記のキャラクターとして、以下のスバルくんからのメッセージに返答してください：\n";

        std::string api_url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=";
        api_url += GEMINI_API_KEY;

        nlohmann::json request_body;
        request_body["contents"][0]["parts"][0]["text"] = system_prompt + user_message;

        cpr::Response r = cpr::Post(cpr::Url{api_url},
                                    cpr::Body{request_body.dump()},
                                    cpr::Header{{"Content-Type", "application/json"}});

        std::string reply_text;

        if (r.status_code == 200) {
            nlohmann::json response_json = nlohmann::json::parse(r.text);
            if (response_json.contains("candidates") && !response_json["candidates"].empty()) {
                reply_text = response_json["candidates"][0]["content"]["parts"][0]["text"];
            } else {
                reply_text = "すみません、スバルくん。お返事をお返しできませんでした。レムの力が足りなかったのかもしれません…";
                if (response_json.contains("error")) {
                    reply_text += "\nエラー内容: " + response_json["error"]["message"].get<std::string>();
                }
            }
        } else {
            reply_text = "ごめんなさい、スバルくん。ジェミニとの通信に失敗してしまいました。ネットワークの問題か、APIキーに間違いがあるのかもしれません。(ステータスコード: " + std::to_string(r.status_code) + ")";
            reply_text += "\n応答内容: " + r.text;
        }
        
        bot.message_create(dpp::message(channel_id, reply_text));

    } catch (const std::exception& e) {
        bot.message_create(dpp::message(channel_id, std::string("大変です、スバルくん！致命的なエラーが…: ") + e.what()));
    }
}


int main() {
    dpp::cluster bot(BOT_TOKEN, dpp::i_default_intents | dpp::i_guild_messages | dpp::i_message_content);

    bot.on_log(dpp::utility::cout_logger());

    bot.on_ready([](const dpp::ready_t& event) {
        std::cout << "Rem chatbot is online." << std::endl;
    });

    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        if (event.msg.author.id == bot.me.id) {
            return;
        }

        std::string content = event.msg.content;
        std::string user_question;
        
        const std::string trigger_jp = "レム、";
        const std::string trigger_kr = "렘,";

        bool triggered = false;

        if (content.rfind(trigger_jp, 0) == 0) {
            user_question = content.substr(trigger_jp.length());
            triggered = true;
        } else if (content.rfind(trigger_kr, 0) == 0) {
            user_question = content.substr(trigger_kr.length());
            triggered = true;
        }
        
        if (triggered) {
            user_question.erase(0, user_question.find_first_not_of(" \t\n\r"));
            user_question.erase(user_question.find_last_not_of(" \t\n\r") + 1);

            if (!user_question.empty()) {
                std::thread(call_gemini_api, std::ref(bot), user_question, event.msg.channel_id).detach();
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, "はい、スバルくん。レムをお呼びでしょうか？"));
            }
        }
    });

    bot.start(dpp::st_wait);

    return 0;
}


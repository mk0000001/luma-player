package io.github.mk0000001.lumaplayer;

import org.json.JSONException;
import org.json.JSONObject;

final class LanJson {
    private static final int MAX_DEPTH = 16;

    private LanJson() {}

    static JSONObject parseObject(String text) throws JSONException {
        // Android's JSONObject parses nested objects and arrays recursively.
        // Check the wire format's structure before passing it to that parser.
        char[] closers = new char[MAX_DEPTH];
        int depth = 0;
        boolean started = false;
        boolean inString = false;
        boolean inLiteral = false;
        for (int index = 0; index < text.length(); index++) {
            char token = text.charAt(index);
            if (inString) {
                if (token == '\\') {
                    index++;
                } else if (token == '"') {
                    inString = false;
                }
                continue;
            }
            if (token == ' ' || token == '\t' || token == '\r' || token == '\n') {
                inLiteral = false;
                continue;
            }
            if (depth == 0) {
                if (started || token != '{') {
                    throw new JSONException("Expected one LAN JSON object");
                }
                started = true;
            }
            switch (token) {
                case '"' -> {
                    if (inLiteral) {
                        throw new JSONException("Invalid LAN JSON token");
                    }
                    inString = true;
                }
                case '{', '[' -> {
                    if (depth == MAX_DEPTH) {
                        throw new JSONException("LAN JSON exceeds the nesting limit");
                    }
                    closers[depth++] = token == '{' ? '}' : ']';
                    inLiteral = false;
                }
                case '}', ']' -> {
                    if (closers[--depth] != token) {
                        throw new JSONException("Invalid LAN JSON nesting");
                    }
                    inLiteral = false;
                }
                case ',', ':' -> inLiteral = false;
                case '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '-', '+', 'e', 'E',
                        't', 'r', 'u', 'f', 'a', 'l', 's', 'n' -> inLiteral = true;
                default -> throw new JSONException("Invalid LAN JSON token");
            }
        }
        if (!started || inString || depth != 0) {
            throw new JSONException("Incomplete LAN JSON object");
        }
        return new JSONObject(text);
    }
}

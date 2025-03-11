#include <json.hpp>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <utility>

using json = nlohmann::json;

struct TerminalSaxHandler : public json::json_sax_t {
    // The resulting JSON object
    json result;
    // The stack used to track the current node (object or array)
    std::vector<nlohmann::json*> stack;
    // The current key when processing an object
    std::string current_key;

    TerminalSaxHandler() {
        // Initialize the stack with a pointer to the root node
        stack.push_back(&result);
    }

    // Called when a null is encountered
    bool null() override {
        add_value(nullptr);
        return true;
    }

    // Called when a boolean is encountered
    bool boolean(bool val) override {
        add_value(val);
        return true;
    }

    // Called when an integer is encountered
    bool number_integer(number_integer_t val) override {
        add_value(val);
        return true;
    }

    // Called when an unsigned integer is encountered
    bool number_unsigned(number_unsigned_t val) override {
        add_value(val);
        return true;
    }

    // Called when a floating-point number is encountered
    bool number_float(number_float_t val, const std::string & s) override {
        add_value(val);
        return true;
    }

    // Called when a string is encountered
    bool string(std::string & s) override {
        add_value(s);
        return true;
    }

    // Called when binary data is encountered
    bool binary(json::binary_t & val) override {
        add_value(val);
        return true;
    }

    // Called at the start of an object
    bool start_object(std::size_t elements) override {
        json obj = json::object();
        // Add the new object and push it onto the stack
        add_value(obj, true);
        return true;
    }

    // Called for each object key
    bool key(std::string & s) override {
        current_key = s;
        return true;
    }

    // Called at the end of an object
    bool end_object() override {
        if (!stack.empty())
            stack.pop_back();
        return true;
    }

    // Called at the start of an array
    bool start_array(std::size_t elements) override {
        json arr = json::array();
        // Add the new array and push it onto the stack
        add_value(arr, true);
        return true;
    }

    // Called at the end of an array
    bool end_array() override {
        if (!stack.empty())
            stack.pop_back();
        return true;
    }

    // Called when a parse error occurs
    bool parse_error(std::size_t position, const std::string & last_token,
                     const json::exception & ex) override {
        std::cerr << "Parse error at position " << position 
                  << " near token \"" << last_token << "\": " 
                  << ex.what() << std::endl;
        return false;
    }

private:
    // Helper function to add a value to the current JSON node.
    // If push_new is true, then the new node becomes the new current node (pushed onto the stack).
    template <typename T>
    void add_value(T&& value, bool push_new = false) {
        json* top = stack.back();
        if (top->is_array()) {
            top->push_back(std::forward<T>(value));
            if (push_new)
                stack.push_back(&top->back());
        }
        else if (top->is_object()) {
            (*top)[current_key] = std::forward<T>(value);
            if (push_new)
                stack.push_back(&(*top)[current_key]);
        }
        else {
            // Fallback: replace the value
            *top = std::forward<T>(value);
        }
    }
};

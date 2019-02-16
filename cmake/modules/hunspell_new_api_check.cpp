#include <hunspell/hunspell.hxx>
#include <string>
#include <vector>

int main()
{
    Hunspell hunspell("file.aff", "dict.dic");

    std::string word = "Something";
    std::vector<std::string> suggestions = hunspell.suggest(word);

    hunspell.spell(word);
    return 0;
}

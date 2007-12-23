#include "Tech.h"

#include "Effect.h"
#include "../universe/Parser.h"
#include "../universe/ParserUtil.h"
#include "../util/MultiplayerCommon.h"
#include "../util/OptionsDB.h"
#include "../util/AppInterface.h"   // for Logger()

#include <boost/lexical_cast.hpp>

#include <sstream>
#include <fstream>

std::string DumpIndent();

extern int g_indent;

struct store_tech_impl
{
    template <class T1, class T2, class T3>
    struct result {typedef void type;};
    template <class T>
    void operator()(TechManager::TechContainer& techs, std::set<std::string>& categories_seen, const T& tech) const
    {
        categories_seen.insert(tech->Category());
        if (techs.get<TechManager::NameIndex>().find(tech->Name()) != techs.get<TechManager::NameIndex>().end()) {
            std::string error_str = "ERROR: More than one tech in techs.txt has the name " + tech->Name();
            throw std::runtime_error(error_str.c_str());
        }
        if (tech->Prerequisites().find(tech->Name()) != tech->Prerequisites().end()) {
            std::string error_str = "ERROR: Tech " + tech->Name() + " depends on itself!";
            throw std::runtime_error(error_str.c_str());
        }
        techs.insert(tech);
    }
};

namespace {
    void NextTechs(std::vector<const Tech*>& retval, const std::set<std::string>& known_techs, std::set<const Tech*>& checked_techs,
                   TechManager::iterator it, TechManager::iterator end_it)
    {
        if (checked_techs.find(*it) != checked_techs.end())
            return;

        if (known_techs.find((*it)->Name()) == known_techs.end() && it != end_it) {
            std::vector<const Tech*> stack;
            stack.push_back(*it);
            while (!stack.empty()) {
                const Tech* current_tech = stack.back();
                unsigned int starting_stack_size = stack.size();
                const std::set<std::string>& prereqs = current_tech->Prerequisites();
                bool all_prereqs_known = true;
                for (std::set<std::string>::const_iterator prereq_it = prereqs.begin(); prereq_it != prereqs.end(); ++prereq_it) {
                    const Tech* prereq_tech = GetTech(*prereq_it);
                    bool prereq_unknown = known_techs.find(prereq_tech->Name()) == known_techs.end();
                    if (prereq_unknown)
                        all_prereqs_known = false;
                    if (checked_techs.find(prereq_tech) == checked_techs.end() && prereq_unknown)
                        stack.push_back(prereq_tech);
                }
                if (starting_stack_size == stack.size()) {
                    stack.pop_back();
                    checked_techs.insert(current_tech);
                    if (all_prereqs_known)
                        retval.push_back(current_tech);
                }
            }
        }
    }

    const Tech* Cheapest(const std::vector<const Tech*>& next_techs)
    {
        if (next_techs.empty())
            return 0;

        double min_price = next_techs[0]->ResearchCost() * next_techs[0]->ResearchTurns();
        int min_index = 0;
        for (unsigned int i = 0; i < next_techs.size(); ++i) {
            double price = next_techs[i]->ResearchCost() * next_techs[i]->ResearchTurns();
            if (price < min_price) {
                min_price = price;
                min_index = i;
            }
        }

        return next_techs[min_index];
    }

    const phoenix::function<store_tech_impl> store_tech_;
}


///////////////////////////////////////////////////////////
// Tech                                                  //
///////////////////////////////////////////////////////////
Tech::Tech(const std::string& name,
           const std::string& description,
           const std::string& short_description,
           const std::string& category,
           TechType type,
           double research_cost,
           int research_turns,
           const std::vector<boost::shared_ptr<const Effect::EffectsGroup> >& effects,
           const std::set<std::string>& prerequisites,
           const std::vector<ItemSpec>& unlocked_items,
           const std::string& graphic) :
    m_name(name),
    m_description(description),
    m_short_description(short_description),
    m_category(category),
    m_type(type),
    m_research_cost(research_cost),
    m_research_turns(research_turns),
    m_effects(effects),
    m_prerequisites(prerequisites),
    m_unlocked_items(unlocked_items),
    m_graphic(graphic)
{}

const std::string& Tech::Name() const
{
    return m_name;
}

const std::string& Tech::Description() const
{
    return m_description;
}

const std::string& Tech::ShortDescription() const
{
    return m_short_description;
}

std::string Tech::Dump() const
{
    using boost::lexical_cast;

    std::string retval = DumpIndent() + "Tech\n";
    ++g_indent;
    retval += DumpIndent() + "name = \"" + m_name + "\"\n";
    retval += DumpIndent() + "description = \"" + m_description + "\"\n";
    retval += DumpIndent() + "shortdescription = \"" + m_short_description + "\"\n";
    retval += DumpIndent() + "techtype = ";
    switch (m_type) {
    case TT_THEORY:      retval += "Theory"; break;
    case TT_APPLICATION: retval += "Application"; break;
    case TT_REFINEMENT:  retval += "Refinement"; break;
    default: retval += "?"; break;
    }
    retval += "\n";
    retval += DumpIndent() + "category = \"" + m_category + "\"\n";
    retval += DumpIndent() + "researchcost = " + lexical_cast<std::string>(m_research_cost) + "\n";
    retval += DumpIndent() + "researchturns = " + lexical_cast<std::string>(m_research_turns) + "\n";
    retval += DumpIndent() + "prerequisites = ";
    if (m_prerequisites.empty()) {
        retval += "[]\n";
    } else if (m_prerequisites.size() == 1) {
        retval += "\"" + *m_prerequisites.begin() + "\"\n";
    } else {
        retval += "[\n";
        ++g_indent;
        for (std::set<std::string>::const_iterator it = m_prerequisites.begin(); it != m_prerequisites.end(); ++it) {
            retval += DumpIndent() + "\"" + *it + "\"\n";
        }
        --g_indent;
        retval += DumpIndent() + "]\n";
    }
    retval += DumpIndent() + "unlock = ";
    if (m_unlocked_items.empty()) {
        retval += "[]\n";
    } else if (m_unlocked_items.size() == 1) {
        retval += m_unlocked_items[0].Dump();
    } else {
        retval += "[\n";
        ++g_indent;
        for (unsigned int i = 0; i < m_unlocked_items.size(); ++i) {
            retval += DumpIndent() + m_unlocked_items[i].Dump();
        }
        --g_indent;
        retval += DumpIndent() + "]\n";
    }
    if (!m_effects.empty()) {
        if (m_effects.size() == 1) {
            retval += DumpIndent() + "effectsgroups =\n";
            ++g_indent;
            retval += m_effects[0]->Dump();
            --g_indent;
        } else {
            retval += DumpIndent() + "effectsgroups = [\n";
            ++g_indent;
            for (unsigned int i = 0; i < m_effects.size(); ++i) {
                retval += m_effects[i]->Dump();
            }
            --g_indent;
            retval += DumpIndent() + "]\n";
        }
    }
    retval += DumpIndent() + "graphic = \"" + m_graphic + "\"\n";
    --g_indent;
    return retval;
}

TechType Tech::Type() const
{
    return m_type;
}

const std::string& Tech::Category() const
{
    return m_category;
}

double Tech::ResearchCost() const
{
    return m_research_cost;
}

int Tech::ResearchTurns() const
{
    return m_research_turns;
}

const std::vector<boost::shared_ptr<const Effect::EffectsGroup> >& Tech::Effects() const
{
    return m_effects;
}

const std::set<std::string>& Tech::Prerequisites() const
{
    return m_prerequisites;
}

const std::string& Tech::Graphic() const
{
    return m_graphic;
}

const std::vector<ItemSpec>& Tech::UnlockedItems() const
{
    return m_unlocked_items;
}

const std::set<std::string>& Tech::UnlockedTechs() const
{
    return m_unlocked_techs;
}


///////////////////////////////////////////////////////////
// ItemSpec                                        //
///////////////////////////////////////////////////////////
ItemSpec::ItemSpec() :
    type(INVALID_UNLOCKABLE_ITEM_TYPE),
    name("")
{}

ItemSpec::ItemSpec(UnlockableItemType type_, const std::string& name_) :
    type(type_),
    name(name_)
{}

std::string ItemSpec::Dump() const
{
    std::string retval = "Item type = ";
    switch (type) {
    case UIT_BUILDING:  retval += "Building"; break;
    case UIT_SHIP_PART: retval += "ShipPart"; break;
    case UIT_SHIP_HULL: retval += "ShipHull"; break;
    default:            retval += "?"; break;
    }
    retval += " name = \"" + name + "\"\n";
    return retval;
}


///////////////////////////////////////////////////////////
// TechManager                                           //
///////////////////////////////////////////////////////////
// static(s)
TechManager* TechManager::s_instance = 0;

const Tech* TechManager::GetTech(const std::string& name)
{
    iterator it = m_techs.get<NameIndex>().find(name);
    return it == m_techs.get<NameIndex>().end() ? 0 : *it;
}

const std::vector<std::string>& TechManager::CategoryNames() const
{
    return m_categories;
}

std::vector<const Tech*> TechManager::AllNextTechs(const std::set<std::string>& known_techs)
{
    std::vector<const Tech*> retval;
    std::set<const Tech*> checked_techs;
    iterator end_it = m_techs.get<NameIndex>().end();
    for (iterator it = m_techs.get<NameIndex>().begin(); it != end_it; ++it) {
        NextTechs(retval, known_techs, checked_techs, it, end_it);
    }
    return retval;
}

const Tech* TechManager::CheapestNextTech(const std::set<std::string>& known_techs)
{
    return Cheapest(AllNextTechs(known_techs));
}

std::vector<const Tech*> TechManager::NextTechsTowards(const std::set<std::string>& known_techs,
                                                       const std::string& desired_tech)
{
    std::vector<const Tech*> retval;
    std::set<const Tech*> checked_techs;
    NextTechs(retval, known_techs, checked_techs, m_techs.get<NameIndex>().find(desired_tech), m_techs.get<NameIndex>().end());
    return retval;
}

const Tech* TechManager::CheapestNextTechTowards(const std::set<std::string>& known_techs,
                                                 const std::string& desired_tech)
{
    return Cheapest(NextTechsTowards(known_techs, desired_tech));
}

TechManager::iterator TechManager::begin() const
{
    return m_techs.get<NameIndex>().begin();
}

TechManager::iterator TechManager::end() const
{
    return m_techs.get<NameIndex>().end();
}

TechManager::category_iterator TechManager::category_begin(const std::string& name) const
{
    return m_techs.get<CategoryIndex>().lower_bound(name);
}

TechManager::category_iterator TechManager::category_end(const std::string& name) const
{
    return m_techs.get<CategoryIndex>().upper_bound(name);
}

TechManager::TechManager()
{
    if (s_instance)
        throw std::runtime_error("Attempted to create more than one TechManager.");

    s_instance = this;

    std::string settings_dir = GetOptionsDB().Get<std::string>("settings-dir");
    if (!settings_dir.empty() && settings_dir[settings_dir.size() - 1] != '/')
        settings_dir += '/';
    std::string filename = settings_dir + "techs.txt";
    std::ifstream ifs(filename.c_str());
    std::set<std::string> categories_seen_in_techs;
    std::string input;
    std::getline(ifs, input, '\0');
    ifs.close();
    using namespace boost::spirit;
    using namespace phoenix;
    parse_info<const char*> result =
        parse(input.c_str(),
              as_lower_d[*(tech_p[store_tech_(var(m_techs), var(categories_seen_in_techs), arg1)]
                           | tech_category_p[push_back_(var(m_categories), arg1)])],
              skip_p);
    if (!result.full)
        ReportError(std::cerr, input.c_str(), result);

    std::set<std::string> empty_defined_categories;
    for (unsigned int i = 0; i < m_categories.size(); ++i) {
        std::set<std::string>::iterator it = categories_seen_in_techs.find(m_categories[i]);
        if (it == categories_seen_in_techs.end()) {
            empty_defined_categories.insert(m_categories[i]);
        } else {
            categories_seen_in_techs.erase(it);
        }
    }

    if (!empty_defined_categories.empty()) {
        std::stringstream stream;
        for (std::set<std::string>::iterator it = empty_defined_categories.begin(); it != empty_defined_categories.end(); ++it) {
            stream << " \"" << *it << "\"";
        }
        std::string error_str = "ERROR: The following categories were defined in techs.txt, but no "
            "techs were defined that fell within them:" + stream.str();
        throw std::runtime_error(error_str.c_str());
    }

    if (!categories_seen_in_techs.empty()) {
        std::stringstream stream;
        for (std::set<std::string>::iterator it = categories_seen_in_techs.begin(); it != categories_seen_in_techs.end(); ++it) {
            stream << " \"" << *it << "\"";
        }
        std::string error_str = "ERROR: The following categories were never defined in techs.txt, but some "
            "techs were defined that fell within them:" + stream.str();
        throw std::runtime_error(error_str.c_str());
    }

    std::string illegal_dependency_str = FindIllegalDependencies();
    if (!illegal_dependency_str.empty()) {
        throw std::runtime_error(illegal_dependency_str.c_str());
    }

    std::string cycle_str = FindFirstDependencyCycle();
    if (!cycle_str.empty()) {
        throw std::runtime_error(cycle_str.c_str());
    }

    // fill in the unlocked techs data for each loaded tech
    for (iterator it = begin(); it != end(); ++it) {
        const std::set<std::string>& prereqs = (*it)->Prerequisites();
        for (std::set<std::string>::const_iterator prereq_it = prereqs.begin(); prereq_it != prereqs.end(); ++prereq_it) {
            const_cast<Tech*>(GetTech(*prereq_it))->m_unlocked_techs.insert((*it)->Name());
        }
    }

    std::string redundant_dependency = FindRedundantDependency();
    if (!redundant_dependency.empty()) {
        throw std::runtime_error(redundant_dependency.c_str());
    }

#ifdef OUTPUT_TECH_LIST
    for (iterator it = begin(); it != end(); ++it) {
        const Tech* tech = *it;
        std::cerr << UserString(tech->Name()) << " (" 
                  << UserString(tech->Category()) << " "
                  << UserString(boost::lexical_cast<std::string>(tech->Type())) << ") - "
                  << tech->Graphic() << std::endl;
    }
#endif
}

std::string TechManager::FindIllegalDependencies()
{
    assert(!m_techs.empty());

    std::string retval;
    for (iterator it = begin(); it != end(); ++it) {
        const Tech* tech = *it;
        TechType tech_type = tech->Type();
        const std::set<std::string>& prereqs = tech->Prerequisites();
        for (std::set<std::string>::const_iterator prereq_it = prereqs.begin(); prereq_it != prereqs.end(); ++prereq_it) {
            const Tech* prereq_tech = GetTech(*prereq_it);
            if (!prereq_tech) {
                retval += "ERROR: Tech \"" + tech->Name() + "\" requires a missing or malformed tech as its prerequisite.\"\n";
                continue;
            }
            TechType prereq_type = prereq_tech->Type();
            if (tech_type == TT_THEORY && prereq_type != TT_THEORY)
                retval += "ERROR: Theory tech \"" + tech->Name() + "\" requires non-Theory tech \"" + prereq_tech->Name() + "\"; Theory techs can only require other Theory techs.\n";
            if (prereq_type == TT_REFINEMENT && tech_type != TT_REFINEMENT)
                retval += "ERROR: Non-Refinement Tech \"" + tech->Name() + "\" requires Refinement tech \"" + prereq_tech->Name() + "\"; Refinement techs cannot be requirements for anything but other Refinement techs.\n";
        }
    }
    return retval;
}

std::string TechManager::FindFirstDependencyCycle()
{
    assert(!m_techs.empty());

    std::set<const Tech*> checked_techs; // the list of techs that are not part of any cycle
    for (iterator it = begin(); it != end(); ++it) {
        if (checked_techs.find(*it) != checked_techs.end())
            continue;

        std::vector<const Tech*> stack;
        stack.push_back(*it);
        while (!stack.empty()) {
            // Examine the tech on top of the stack.  If the tech has no prerequisite techs, or if all
            // of its prerequisite techs have already been checked, pop it off the stack and mark it as
            // checked; otherwise, push all its unchecked prerequisites onto the stack.
            const Tech* current_tech = stack.back();
            unsigned int starting_stack_size = stack.size();
            const std::set<std::string>& prereqs = current_tech->Prerequisites();
            for (std::set<std::string>::const_iterator prereq_it = prereqs.begin(); prereq_it != prereqs.end(); ++prereq_it) {
                const Tech* prereq_tech = GetTech(*prereq_it);
                if (checked_techs.find(prereq_tech) == checked_techs.end()) {
                    // since this is not a checked prereq, see if it is already in the stack somewhere; if so, we have a cycle
                    std::vector<const Tech*>::reverse_iterator stack_duplicate_it =
                        std::find(stack.rbegin(), stack.rend(), prereq_tech);
                    if (stack_duplicate_it != stack.rend()) {
                        std::stringstream stream;
                        std::string current_tech_name = prereq_tech->Name();
                        stream << "ERROR: Tech dependency cycle found in techs.txt (A <-- B means A is a prerequisite of B): \""
                               << current_tech_name << "\"";
                        for (std::vector<const Tech*>::reverse_iterator stack_it = stack.rbegin();
                             stack_it != stack_duplicate_it;
                             ++stack_it) {
                            if ((*stack_it)->Prerequisites().find(current_tech_name) != (*stack_it)->Prerequisites().end()) {
                                current_tech_name = (*stack_it)->Name();
                                stream << " <-- \"" << current_tech_name << "\"";
                            }
                        }
                        stream << " <-- \"" << prereq_tech->Name() << "\" ... ";
                        return stream.str();
                    } else {
                        stack.push_back(prereq_tech);
                    }
                }
            }
            if (starting_stack_size == stack.size()) {
                stack.pop_back();
                checked_techs.insert(current_tech);
            }
        }
    }
    return "";
}

std::string TechManager::FindRedundantDependency()
{
    assert(!m_techs.empty());

    for (iterator it = begin(); it != end(); ++it) {
        const Tech* tech = *it;
        std::set<std::string> prereqs = tech->Prerequisites();
        std::map<std::string, std::string> techs_unlocked_by_prereqs;
        for (std::set<std::string>::const_iterator prereq_it = prereqs.begin(); prereq_it != prereqs.end(); ++prereq_it) {
            const Tech* prereq_tech = GetTech(*prereq_it);
            AllChildren(prereq_tech, techs_unlocked_by_prereqs);
        }
        for (std::set<std::string>::const_iterator prereq_it = prereqs.begin(); prereq_it != prereqs.end(); ++prereq_it) {
            std::map<std::string, std::string>::const_iterator map_it = techs_unlocked_by_prereqs.find(*prereq_it);
            if (map_it != techs_unlocked_by_prereqs.end()) {
                std::stringstream stream;
                stream << "ERROR: Redundant dependency found in tech.txt (A <-- B means A is a prerequisite of B): "
                       << map_it->second << " <-- " << map_it->first << ", "
                       << map_it->first << " <-- " << (*it)->Name() << ", "
                       << map_it->second << " <-- " << (*it)->Name() << "; remove the " << map_it->second << " <-- " << (*it)->Name()
                       << " dependency.";
                return stream.str();
            }
        }
    }
    return "";
}

void TechManager::AllChildren(const Tech* tech, std::map<std::string, std::string>& children)
{
    const std::set<std::string>& unlocked_techs = tech->UnlockedTechs();
    for (std::set<std::string>::const_iterator it = unlocked_techs.begin(); it != unlocked_techs.end(); ++it) {
        children[*it] = tech->Name();
        AllChildren(GetTech(*it), children);
    }
}

TechManager& TechManager::GetTechManager()
{
    static TechManager manager;
    return manager;
}


///////////////////////////////////////////////////////////
// Free Functions                                        //
///////////////////////////////////////////////////////////
TechManager& GetTechManager()
{
    return TechManager::GetTechManager();
}

const Tech* GetTech(const std::string& name)
{
    return GetTechManager().GetTech(name);
}

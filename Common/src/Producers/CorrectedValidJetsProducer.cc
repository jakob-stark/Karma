#include "Karma/Common/interface/Producers/CorrectedValidJetsProducer.h"

// -- constructor
karma::CorrectedValidJetsProducer::CorrectedValidJetsProducer(const edm::ParameterSet& config, const karma::CorrectedValidJetsProducerGlobalCache* globalCache) : m_configPSet(config) {
    // -- register products
    produces<karma::JetCollection>();

    // -- process configuration

    // -- declare which collections are consumed and create tokens
    karmaEventToken = consumes<karma::Event>(m_configPSet.getParameter<edm::InputTag>("karmaEventSrc"));
    karmaJetCollectionToken = consumes<karma::JetCollection>(m_configPSet.getParameter<edm::InputTag>("karmaJetCollectionSrc"));

    const auto& jec = m_configPSet.getParameter<std::string>("jecVersion");
    const auto& jecAlgoName = m_configPSet.getParameter<std::string>("jecAlgoName");
    const auto& jecLevels = m_configPSet.getParameter<std::vector<std::string>>("jecLevels");

    // -- if JEC should not be taken from the global tag,
    //    initialize FactorizedJetCorrectors from text files

    if (!globalCache->jecFromGlobalTag_) {

        // set up a FactorizedJetCorrector (one per stream)
        std::vector<JetCorrectorParameters> jecParameters;
        for (const auto& jecLevel : jecLevels) {
            jecParameters.push_back(
                JetCorrectorParameters(
                    jec + "_" + jecLevel + "_" + jecAlgoName + ".txt"
                )
            );
            std::cout << "[CorrectedValidJetsProducer] Loaded JEC file '" <<
                         jec << "_" << jecLevel << "_" << jecAlgoName << ".txt" << "'" << std::endl;
        }
        if (!jecParameters.empty()) {
            m_jetCorrector = std::unique_ptr<FactorizedJetCorrector>(new FactorizedJetCorrector(jecParameters));
        }

        // set up a separate L1 corrector (needed for type-I MET)
        m_jetCorrector_L1 = std::unique_ptr<FactorizedJetCorrector>(
            new FactorizedJetCorrector({
                JetCorrectorParameters(
                    jec + "_L1FastJet_" + jecAlgoName + ".txt"
                )
            })
        );
        // set up a separate L1RC corrector (needed for type-I MET)
        m_jetCorrector_L1RC = std::unique_ptr<FactorizedJetCorrector>(
            new FactorizedJetCorrector({
                JetCorrectorParameters(
                    jec + "_L1RC_" + jecAlgoName + ".txt"
                )
            })
        );
        // set up a JetCorrectionUncertainty (one per stream)
        m_jetCorrectionUncertainty = std::unique_ptr<JetCorrectionUncertainty>(new JetCorrectionUncertainty(
            jec + "_Uncertainty_" + jecAlgoName + ".txt"
        ));
        std::cout << "[CorrectedValidJetsProducer] Loaded JEU file '" <<
                     jec << "_Uncertainty_" << jecAlgoName << ".txt" << "'" << std::endl;
    }
    else {
        // issue a warning in case both GT and text file are specified
        std::cout << "[CorrectedValidJetsProducer] WARNING: JEC and JEU will be taken from global tag, but parameter "
                  << "'jecVersion' parameter points to text files '"
                  << jec << "_..._" << jecAlgoName << ".txt" << "': the text files will be ignored!" << std::endl;
    }

    // retrieve the configured JEC shift magnitude
    m_jecUncertaintyShift = m_configPSet.getParameter<double>("jecUncertaintyShift");
}


// -- destructor
karma::CorrectedValidJetsProducer::~CorrectedValidJetsProducer() {
}


// -- static member functions

/*static*/ std::unique_ptr<karma::CorrectedValidJetsProducerGlobalCache> karma::CorrectedValidJetsProducer::initializeGlobalCache(const edm::ParameterSet& pSet) {
    // -- create the GlobalCache
    return std::unique_ptr<karma::CorrectedValidJetsProducerGlobalCache>(new karma::CorrectedValidJetsProducerGlobalCache(pSet));
}

/*static*/ std::shared_ptr<karma::CorrectedValidJetsProducerRunCache> karma::CorrectedValidJetsProducer::globalBeginRun(const edm::Run& run, const edm::EventSetup& setup, const karma::CorrectedValidJetsProducer::GlobalCache* globalCache) {
    // -- create the RunCache
    auto runCache = std::make_shared<karma::CorrectedValidJetsProducerRunCache>(globalCache->pSet_);

    // -- if JEC should be taken from the global tag,
    //    initialize FactorizedJetCorrectors from database

    if (globalCache->jecFromGlobalTag_) {

        const auto& jecAlgoName = globalCache->pSet_.getParameter<std::string>("jecAlgoName");
        const auto& jecLevels = globalCache->pSet_.getParameter<std::vector<std::string>>("jecLevels");

        // retrieve the jet energy corrections record from the conditions database
        edm::ESHandle<JetCorrectorParametersCollection> parameters;
        setup.get<JetCorrectionsRecord>().get(jecAlgoName, parameters);

        // -- retrieve the jet energy correction parameters

        // set up a main jet energy corrector
        std::vector<JetCorrectorParameters> jecParameters;
        for (const auto& jecLevel : jecLevels) {
            jecParameters.push_back((*parameters)[jecLevel]);
            std::cout << "[CorrectedValidJetsProducer] Loaded JEC from global tag." << std::endl;
        }
        if (!jecParameters.empty()) {
            runCache->jetCorrector_ = std::unique_ptr<FactorizedJetCorrector>(new FactorizedJetCorrector(jecParameters));
        }

        // set up a separate L1 corrector (needed for type-I MET)
        std::vector<JetCorrectorParameters> jecParametersL1;
        jecParametersL1.push_back((*parameters)["L1FastJet"]);
        runCache->jetCorrectorL1_ = std::unique_ptr<FactorizedJetCorrector>(new FactorizedJetCorrector(jecParametersL1));

        // set up a separate L1RC corrector (needed for type-I MET)
        std::vector<JetCorrectorParameters> jecParametersL1RC;
        jecParametersL1RC.push_back((*parameters)["L1RC"]);
        runCache->jetCorrectorL1RC_ = std::unique_ptr<FactorizedJetCorrector>(new FactorizedJetCorrector(jecParametersL1RC));

        // set up a jet correction uncertainty provider
        const JetCorrectorParameters& jeuParameters = (*parameters)["Uncertainty"];
        runCache->jetCorrectionUncertainty_ = std::unique_ptr<JetCorrectionUncertainty>(new JetCorrectionUncertainty(jeuParameters));
        std::cout << "[CorrectedValidJetsProducer] Loaded JEU from global tag." << std::endl;
    }

    return runCache;
}

// -- member functions

void karma::CorrectedValidJetsProducer::produce(edm::Event& event, const edm::EventSetup& setup) {
    std::unique_ptr<karma::JetCollection> outputJetCollection(new karma::JetCollection());

    // -- get object collections for event

    // pileup density
    karma::util::getByTokenOrThrow(event, this->karmaEventToken, this->karmaEventHandle);
    // jet collection
    karma::util::getByTokenOrThrow(event, this->karmaJetCollectionToken, this->karmaJetCollectionHandle);

    // -- route to appropriate set of correctors
    FactorizedJetCorrector* jetCorrector;
    FactorizedJetCorrector* jetCorrectorL1;
    FactorizedJetCorrector* jetCorrectorL1RC;
    JetCorrectionUncertainty* jetCorrectionUncertainty;

    if (globalCache()->jecFromGlobalTag_) {
        jetCorrector = runCache()->jetCorrector_.get();
        jetCorrectorL1 = runCache()->jetCorrectorL1_.get();
        jetCorrectorL1RC = runCache()->jetCorrectorL1RC_.get();
        jetCorrectionUncertainty = runCache()->jetCorrectionUncertainty_.get();
    }
    else {
        jetCorrector = m_jetCorrector.get();
        jetCorrectorL1 = m_jetCorrector_L1.get();
        jetCorrectorL1RC = m_jetCorrector_L1RC.get();
        jetCorrectionUncertainty = m_jetCorrectionUncertainty.get();
    }

    // -- populate outputs

    for (const auto& inputJet : (*this->karmaJetCollectionHandle)) {
        // reject jets which do not pass JetID (if requested)
        if (globalCache()->jetIDProvider_ && !globalCache()->jetIDProvider_->getJetID(inputJet))
            continue;

        // copy jet to output
        outputJetCollection->push_back(inputJet);

        // store L1-corrected p4 in transient map
        setupFactorizedJetCorrector(*jetCorrectorL1, *this->karmaEventHandle, inputJet);
        outputJetCollection->back().transientLVs_["L1"] = outputJetCollection->back().uncorP4 * jetCorrectorL1->getCorrection();

        // store L1RC-corrected p4 in transient map
        setupFactorizedJetCorrector(*jetCorrectorL1RC, *this->karmaEventHandle, inputJet);
        outputJetCollection->back().transientLVs_["L1RC"] = outputJetCollection->back().uncorP4 * jetCorrectorL1RC->getCorrection();

        // apply correction (if any requested)
        if (jetCorrector) {
            setupFactorizedJetCorrector(*jetCorrector, *this->karmaEventHandle, inputJet);
            outputJetCollection->back().p4 = outputJetCollection->back().uncorP4 * jetCorrector->getCorrection();
        }
        else {
            outputJetCollection->back().p4 = outputJetCollection->back().uncorP4;
        }

        // apply uncertainty shift to output jet
        setupFactorProvider(*jetCorrectionUncertainty, inputJet);
        outputJetCollection->back().p4 *= (1.0 + m_jecUncertaintyShift * jetCorrectionUncertainty->getUncertainty(/*bool direction = */ m_jecUncertaintyShift > 0.0));

    }

    // re-sort jets by pT
    std::sort(
        (*outputJetCollection).begin(),
        (*outputJetCollection).end(),
        [](const karma::Jet& jet1, const karma::Jet& jet2) {
            return (jet1.p4.pt() > jet2.p4.pt());
        }
    );

    // move outputs to event tree
    event.put(std::move(outputJetCollection));
}


void karma::CorrectedValidJetsProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    // The following says we do not know what parameters are allowed so do no validation
    // Please change this to state exactly what you do use, even if it is no parameters
    edm::ParameterSetDescription desc;
    desc.setUnknown();
    descriptions.addDefault(desc);
}


//define this as a plug-in
using KarmaCorrectedValidJetsProducer = karma::CorrectedValidJetsProducer;
DEFINE_FWK_MODULE(KarmaCorrectedValidJetsProducer);

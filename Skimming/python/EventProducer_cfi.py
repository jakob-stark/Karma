import FWCore.ParameterSet.Config as cms


dijetEventProducer = cms.EDProducer(
    "EventProducer",
    cms.PSet(
        # -- input sources
        pileupDensitySrc = cms.InputTag("fixedGridRhoFastjetAll"),
        triggerResultsSrc = cms.InputTag("TriggerResults", "", "HLT"),
        #triggerPrescalesSrc = cms.InputTag("patTrigger"),
        primaryVerticesSrc = cms.InputTag("offlineSlimmedPrimaryVertices"),
        goodPrimaryVerticesSrc = cms.InputTag("goodOfflinePrimaryVertices"),

        # -- other configuration

        # name of the HLT process (for HLTConfigProvider)
        hltProcessName = cms.string("HLT"),

        # HLTPrescaleProvider configuration
        hltPrescaleProvider = cms.PSet(
            l1GtRecordInputTag = cms.InputTag("l1GtTriggerMenuLite"),
            l1GtReadoutRecordInputTag = cms.InputTag(""),
            l1GtTriggerMenuLiteInputTag =  cms.InputTag("l1GtTriggerMenuLite"),
        ),

        # interesting trigger paths must match this regex:
        hltRegexes = cms.vstring("HLT_PFJet[0-9]+_v[0-9]+"),
    )
)

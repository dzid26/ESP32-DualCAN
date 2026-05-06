// Curated subset of commaai/opendbc — same list as the design bundle.
// Production should generate from the opendbc manifest at build time.
export type Car = {
  id: string;
  brand: string;
  model: string;
  years: string;
  dbc: string;
  icon?: string;
  popular?: boolean;
};

export const OPENDBC_CARS: Car[] = [
  // Tesla
  { id: 'tesla-m3-2018', brand: 'Tesla', model: 'Model 3', years: '2017+', dbc: 'tesla_model3_party.dbc', icon: '🛻', popular: true },
  { id: 'tesla-my-2020', brand: 'Tesla', model: 'Model Y', years: '2020+', dbc: 'tesla_modelY_party.dbc', icon: '🛻', popular: true },
  { id: 'tesla-ms-2021', brand: 'Tesla', model: 'Model S', years: '2021+', dbc: 'tesla_models_2021.dbc' },
  { id: 'tesla-mx-2021', brand: 'Tesla', model: 'Model X', years: '2021+', dbc: 'tesla_modelx_2021.dbc' },
  // Toyota
  { id: 'toy-rav4-2019',    brand: 'Toyota', model: 'RAV4',    years: '2019+', dbc: 'toyota_rav4_2019_pt.dbc',    popular: true },
  { id: 'toy-prius-2017',   brand: 'Toyota', model: 'Prius',   years: '2017+', dbc: 'toyota_prius_2017_pt.dbc' },
  { id: 'toy-corolla-2020', brand: 'Toyota', model: 'Corolla', years: '2020+', dbc: 'toyota_corolla_2017_pt.dbc' },
  // Honda
  { id: 'hon-civic-2022',  brand: 'Honda', model: 'Civic',  years: '2022+', dbc: 'honda_civic_2022_can_generated.dbc', popular: true },
  { id: 'hon-accord-2018', brand: 'Honda', model: 'Accord', years: '2018+', dbc: 'honda_accord_2018_pt.dbc' },
  { id: 'hon-crv-2017',    brand: 'Honda', model: 'CR-V',   years: '2017+', dbc: 'honda_crv_ex_2017_can_generated.dbc' },
  // Hyundai / Kia / Genesis
  { id: 'hyu-ioniq5-2022',  brand: 'Hyundai', model: 'Ioniq 5', years: '2022+', dbc: 'hyundai_ev_2022.dbc', popular: true },
  { id: 'hyu-elantra-2021', brand: 'Hyundai', model: 'Elantra', years: '2021+', dbc: 'hyundai_kia_generic.dbc' },
  { id: 'kia-ev6-2022',     brand: 'Kia',     model: 'EV6',     years: '2022+', dbc: 'hyundai_ev_2022.dbc' },
  { id: 'gen-g70-2019',     brand: 'Genesis', model: 'G70',     years: '2019+', dbc: 'hyundai_kia_generic.dbc' },
  // VW / Audi
  { id: 'vw-golf-mk7', brand: 'Volkswagen', model: 'Golf', years: 'Mk7',   dbc: 'vw_mqb_2010.dbc' },
  { id: 'aud-a3-2018', brand: 'Audi',       model: 'A3',   years: '2014+', dbc: 'vw_mqb_2010.dbc' },
  // Subaru
  { id: 'sub-outback-2020', brand: 'Subaru', model: 'Outback', years: '2020+', dbc: 'subaru_global_2020_hybrid_generated.dbc' },
  // Ford / Chevy
  { id: 'ford-bronco-sport-2021', brand: 'Ford',      model: 'Bronco Sport', years: '2021+', dbc: 'ford_lincoln_base_pt.dbc' },
  { id: 'che-bolt-2017',          brand: 'Chevrolet', model: 'Bolt',         years: '2017+', dbc: 'gm_global_a_powertrain_generated.dbc' },
  // Mazda / Nissan
  { id: 'maz-cx5-2017',  brand: 'Mazda',  model: 'CX-5', years: '2017+', dbc: 'mazda_2017.dbc' },
  { id: 'nis-leaf-2018', brand: 'Nissan', model: 'Leaf', years: '2018+', dbc: 'nissan_leaf_2018_generated.dbc' },
];

export function loadCar(): Car | null {
  try {
    const id = localStorage.getItem('dc-car');
    return OPENDBC_CARS.find(c => c.id === id) ?? null;
  } catch { return null; }
}

export function saveCar(id: string | null): void {
  try {
    if (id) localStorage.setItem('dc-car', id);
    else localStorage.removeItem('dc-car');
  } catch { /* ignore */ }
}

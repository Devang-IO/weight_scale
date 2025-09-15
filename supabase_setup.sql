-- Create weights table for plant measurements
CREATE TABLE IF NOT EXISTS weights (
    id SERIAL PRIMARY KEY,
    plant_0 DECIMAL(10,2) DEFAULT NULL,
    plant_1 DECIMAL(10,2) DEFAULT NULL,
    plant_2 DECIMAL(10,2) DEFAULT NULL,
    plant_3 DECIMAL(10,2) DEFAULT NULL,
    plant_4 DECIMAL(10,2) DEFAULT NULL,
    plant_5 DECIMAL(10,2) DEFAULT NULL,
    plant_6 DECIMAL(10,2) DEFAULT NULL,
    plant_7 DECIMAL(10,2) DEFAULT NULL,
    plant_8 DECIMAL(10,2) DEFAULT NULL,
    plant_9 DECIMAL(10,2) DEFAULT NULL,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Enable Row Level Security (RLS)
ALTER TABLE weights ENABLE ROW LEVEL SECURITY;

-- Create policy to allow all operations (adjust as needed for your security requirements)
CREATE POLICY "Allow all operations on weights" ON weights
    FOR ALL USING (true) WITH CHECK (true);

-- Create function to automatically update the updated_at timestamp
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ language 'plpgsql';

-- Create trigger to automatically update updated_at
CREATE TRIGGER update_weights_updated_at 
    BEFORE UPDATE ON weights 
    FOR EACH ROW 
    EXECUTE FUNCTION update_updated_at_column();